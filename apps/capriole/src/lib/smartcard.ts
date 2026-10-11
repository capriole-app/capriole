// Signing with a smart card, and long-term validation (PLAN.md phase 11c): what only the desktop shell
// can do (CAPRIOLE.md §6.1). The card's module, its cards and certificates, the PIN and the signing go
// through the shell's PKCS#11 commands; the network — the time-stamping authority, the OCSP responders
// and CRLs the certificates name, their issuers' certificates — through its net_fetch. The engine, in
// the worker, prepares and checks everything; nothing of the document leaves the computer but its
// digest (to the time-stamping authority) and the certificates' serial numbers (to the responders).
import type { EngineClient, SignSpec, ToSign, ValidationData } from '@capriole/engine-client';
import { isDesktop } from './platform';
import { tr } from './i18n/index.svelte';
import { signingWords } from './i18n/signing';

export interface Token {
  slot: number;
  label: string;
  manufacturer: string;
  model: string;
  serial: string;
}

export interface CardCertificate {
  /** CKA_ID, hexadecimal: the private key has the same. */
  id: string;
  label: string;
  /** DER, hexadecimal. */
  der: string;
}

export type Purpose = 'timestamp' | 'ocsp' | 'crl' | 'certificate';

/** What the shell does for a smart card and for the network (apps/desktop/src-tauri/src/lib.rs). */
export interface CardShell {
  tokens(module: string): Promise<Token[]>;
  certificates(module: string, slot: number): Promise<CardCertificate[]>;
  sign(module: string, slot: number, keyId: string, pin: string, toSign: ToSign): Promise<string>;
  /** body and answer in hexadecimal. */
  fetch(purpose: Purpose, url: string, body: string): Promise<string>;
}

declare global {
  interface Window {
    /** Tests only: a shell to drive the smart card dialog in a browser (e2e/smartcard.test.ts). */
    capriolePkcs11Shell?: CardShell;
  }
}

function tauriShell(): CardShell {
  const call = async <T>(command: string, args: Record<string, unknown>): Promise<T> => {
    const { invoke } = await import('@tauri-apps/api/core');
    return invoke<T>(command, args);
  };
  return {
    tokens: (module) => call('pkcs11_tokens', { module }),
    certificates: (module, slot) => call('pkcs11_certificates', { module, slot }),
    sign: (module, slot, keyId, pin, toSign) => call('pkcs11_sign', { module, slot, keyId, pin, toSign }),
    fetch: (purpose, url, body) => call('net_fetch', { purpose, url, body }),
  };
}

/** The shell, where there is one: the desktop app (or a test's). */
export function cardShell(): CardShell | null {
  if (isDesktop) return tauriShell();
  return typeof window !== 'undefined' ? (window.capriolePkcs11Shell ?? null) : null;
}

/** Where the cards' modules usually are (Linux): OpenSC, Bit4id (CNS), the CIE's, SoftHSM (tests). */
export const knownModules = [
  '/usr/lib/x86_64-linux-gnu/opensc-pkcs11.so',
  '/usr/lib/libbit4xpki.so',
  '/usr/local/lib/libcie-pkcs11.so',
  '/usr/lib/softhsm/libsofthsm2.so',
];

const moduleKey = 'capriole.pkcs11Module';
const tsaKey = 'capriole.tsaUrl';

function remembered(key: string): string {
  try {
    return localStorage.getItem(key) ?? '';
  } catch {
    return '';
  }
}

function remember(key: string, value: string): void {
  try {
    localStorage.setItem(key, value);
  } catch {
    // a convenience only
  }
}

export const rememberedModule = () => remembered(moduleKey);
export const rememberModule = (value: string) => remember(moduleKey, value);
export const rememberedTsa = () => remembered(tsaKey);
export const rememberTsa = (value: string) => remember(tsaKey, value);

/** What the shell or the card said, in the language of the interface. */
export function cardProblem(error: unknown): string {
  const text = error instanceof Error ? error.message : String(error);
  const code = /^([a-z-]+):/.exec(text)?.[1];
  const t = tr(signingWords);
  switch (code) {
    case 'pin-incorrect':
      return t.pinIncorrect;
    case 'pin-locked':
      return t.pinLocked;
    case 'module':
      return t.moduleFailed(text.replace(/^module:\s*/, ''));
    case 'no-token':
      return t.noToken;
    case 'no-key':
      return t.noKey;
    case 'no-mechanism':
      return t.noMechanism;
    case 'network':
      return t.network(text.replace(/^network:\s*/, ''));
    case 'url':
      return t.badUrl;
    default:
      return text;
  }
}

/** A signature with a card: what the person chose in the dialog. */
export interface CardSigner {
  module: string;
  slot: number;
  certificate: CardCertificate;
  /** The other certificates of the card (its chain, when it has it), DER hexadecimal. */
  chain: string[];
  pin: string;
  /** The time-stamping authority's URL; empty: no time stamp. */
  tsa: string;
  /** Validation data for the long term (B-LT; with a time stamp, B-LTA). */
  longTerm: boolean;
}

/** What long-term validation could not get: the certificates left without an answer; the document time
 * stamp, when the authority did not give it (the signature is made, B-LT at most). */
export interface LongTermReport {
  unanswered: string[];
  documentTimestamp: string | null;
}

/** The answers for what the signatures' chains need, fetched: OCSP first, else the CRL. */
async function fetchAnswers(
  engine: EngineClient,
  shell: CardShell,
): Promise<{ data: ValidationData; unanswered: string[] }> {
  const data: Required<ValidationData> = { certificates: [], ocsps: [], crls: [] };
  let needs = await engine.validationNeeds(data);
  // Issuers that are not known: fetched from where the certificate says (once), then asked again.
  const missing = needs.filter((n) => n.issuer === null && n.caIssuers.length > 0);
  for (const need of missing) {
    for (const url of need.caIssuers) {
      try {
        const der = await shell.fetch('certificate', url, '');
        if (der.startsWith('30')) {
          data.certificates.push(der);
          break;
        }
      } catch {
        // the next address, else unanswered
      }
    }
  }
  if (missing.length > 0) needs = await engine.validationNeeds(data);
  const unanswered: string[] = [];
  for (const need of needs) {
    if (need.answered) continue;
    let answered = false;
    if (need.ocspRequest) {
      for (const url of need.ocsp) {
        try {
          data.ocsps.push(await shell.fetch('ocsp', url, need.ocspRequest));
          answered = true;
          break;
        } catch {
          // the next responder, else the CRL
        }
      }
    }
    for (const url of answered ? [] : need.crl) {
      try {
        const crl = await shell.fetch('crl', url, '');
        if (!data.crls.includes(crl)) data.crls.push(crl);
        answered = true;
        break;
      } catch {
        // the next address
      }
    }
    if (!answered) unanswered.push(need.subject || tr(signingWords).aCertificate);
  }
  return { data, unanswered };
}

/** Validation data written into the document (B-LT): resolves to the file. */
async function addLongTerm(
  engine: EngineClient,
  shell: CardShell,
  date: string,
  report: LongTermReport,
): Promise<ArrayBuffer | null> {
  const { data, unanswered } = await fetchAnswers(engine, shell);
  report.unanswered.push(...unanswered.filter((u) => !report.unanswered.includes(u)));
  const empty = !data.certificates?.length && !data.ocsps?.length && !data.crls?.length;
  return empty ? null : engine.addValidationData(date, data);
}

/**
 * Signs the open document with the card: the engine prepares, the card signs, the authority stamps
 * (when there is one); then, for the long term, the answers about the chains, a document time stamp
 * and the answers about its authority's chain. Resolves to the file as it ends, and what could not be
 * answered.
 */
export async function signWithCard(
  engine: EngineClient,
  shell: CardShell,
  signer: CardSigner,
  spec: SignSpec,
  date: () => string,
): Promise<{ file: ArrayBuffer; report: LongTermReport }> {
  const timestamp = signer.tsa.trim() !== '';
  const toSign = await engine.signExternalBegin({ ...spec, timestamp }, signer.certificate.der, signer.chain);
  let value: string;
  try {
    value = await shell.sign(signer.module, signer.slot, signer.certificate.id, signer.pin, toSign);
  } catch (error) {
    await engine.signCancel();
    throw error;
  }
  const request = await engine.signExternalValue(value);
  let response: string | null = null;
  if (timestamp && request) {
    try {
      response = await shell.fetch('timestamp', signer.tsa.trim(), request);
    } catch (error) {
      await engine.signCancel();
      throw error;
    }
  }
  let file = await engine.signFinish(response);
  const report: LongTermReport = { unanswered: [], documentTimestamp: null };
  if (!signer.longTerm) return { file, report };
  file = (await addLongTerm(engine, shell, date(), report)) ?? file;
  if (timestamp) {
    // The signature is made: a document time stamp that fails leaves it as it is (said, not thrown).
    const stamp = await engine.timestampBegin(date());
    let stamped: string | null = null;
    try {
      stamped = await shell.fetch('timestamp', signer.tsa.trim(), stamp);
    } catch (error) {
      report.documentTimestamp = cardProblem(error);
    }
    if (stamped === null) {
      await engine.signCancel();
    } else {
      file = await engine.signFinish(stamped);
      file = (await addLongTerm(engine, shell, date(), report)) ?? file;
    }
  }
  return { file, report };
}
