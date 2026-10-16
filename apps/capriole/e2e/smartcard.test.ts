import { expect, test, type Page } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { createPrivateKey, sign as rsaSign } from 'node:crypto';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { withoutFileAccess } from './downloads';

// A smart card and the network as the desktop shell gives them (PLAN.md phase 11c), simulated: the page
// talks to window.capriolePkcs11Shell, whose calls are answered here, in Node — the card signs with the
// key of the test PKI (tests/data/signing/make_ltv_fixtures.sh, "Luca Verdi (test)", PIN 1234), the
// time-stamping authority and the OCSP responder are OpenSSL's (openssl ts -reply, openssl ocsp -index).
const signing = fileURLToPath(new URL('../../../tests/data/signing/', import.meta.url));
const work = mkdtempSync(join(tmpdir(), 'capriole-card-'));

function openssl(...args: string[]): Buffer {
  return execFileSync('openssl', args, { stdio: ['ignore', 'pipe', 'ignore'] });
}

const p12 = ['-in', signing + 'ltv.p12', '-passin', 'pass:capriole'];
const certPem = openssl('pkcs12', ...p12, '-nokeys', '-clcerts').toString('latin1');
const keyPem = openssl('pkcs12', ...p12, '-nocerts', '-nodes').toString('latin1');
writeFileSync(join(work, 'cert.pem'), certPem);
const cardCertificate = openssl('x509', '-in', join(work, 'cert.pem'), '-outform', 'DER').toString('hex');
const caCertificate = openssl('x509', '-in', signing + 'ca.pem', '-outform', 'DER').toString('hex');
const key = createPrivateKey(keyPem);

/** What the simulated shell was asked to fetch: purpose and URL, in order. */
const asked: string[] = [];

function timestamp(request: Buffer): Buffer {
  writeFileSync(join(work, 'query.tsq'), request);
  writeFileSync(join(work, 'tsa-serial'), '01\n');
  writeFileSync(
    join(work, 'tsa.cnf'),
    readFileSync(signing + 'tsa.cnf', 'latin1').replace('./tsa-serial', join(work, 'tsa-serial')),
  );
  openssl(
    'ts',
    '-reply',
    '-queryfile',
    join(work, 'query.tsq'),
    '-inkey',
    signing + 'ltv-tsa.key',
    '-signer',
    signing + 'ltv-tsa.pem',
    '-chain',
    signing + 'ca.pem',
    '-config',
    join(work, 'tsa.cnf'),
    '-out',
    join(work, 'reply.tsr'),
  );
  return readFileSync(join(work, 'reply.tsr'));
}

function ocsp(request: Buffer): Buffer {
  writeFileSync(join(work, 'request.der'), request);
  openssl(
    'ocsp',
    '-index',
    signing + 'ca-index.txt',
    '-CA',
    signing + 'ca.pem',
    '-rsigner',
    signing + 'ocsp.pem',
    '-rkey',
    signing + 'ocsp.key',
    '-reqin',
    join(work, 'request.der'),
    '-respout',
    join(work, 'response.der'),
    '-ndays',
    '36500',
  );
  return readFileSync(join(work, 'response.der'));
}

async function withCard(page: Page) {
  await page.exposeFunction('cardCall', (kind: string, args: Record<string, string>) => {
    switch (kind) {
      case 'tokens':
        return [{ slot: 7, label: 'Capriole', manufacturer: 'Prova', model: 'Carta simulata', serial: '0001' }];
      case 'certificates':
        return [
          { id: '01', label: 'Firma', der: cardCertificate },
          { id: '02', label: 'CA', der: caCertificate },
        ];
      case 'sign': {
        if (args.pin !== '1234') throw new Error("pin-incorrect: the PIN is not the card's");
        if (args.keyId !== '01') throw new Error('no-key: the card has no private key for this certificate');
        // CKM_SHA256_RSA_PKCS over the signed attributes, as a card that hashes does.
        return rsaSign('sha256', Buffer.from(args.data!, 'hex'), key).toString('hex');
      }
      case 'fetch': {
        asked.push(`${args.purpose} ${args.url}`);
        const body = Buffer.from(args.body!, 'hex');
        if (args.purpose === 'timestamp') return timestamp(body).toString('hex');
        if (args.purpose === 'ocsp') return ocsp(body).toString('hex');
        if (args.purpose === 'crl') return readFileSync(signing + 'ca.crl').toString('hex');
        throw new Error(`network: ${args.url}: nothing there`);
      }
    }
    throw new Error(`unknown call ${kind}`);
  });
  await page.addInitScript(() => {
    const call = (window as unknown as { cardCall: (kind: string, args: object) => Promise<unknown> }).cardCall;
    window.capriolePkcs11Shell = {
      tokens: (module) => call('tokens', { module }) as never,
      certificates: (module, slot) => call('certificates', { module, slot }) as never,
      sign: (module, slot, keyId, pin, toSign) =>
        call('sign', { module, slot, keyId, pin, data: toSign.data }) as never,
      fetch: (purpose, url, body) => call('fetch', { purpose, url, body }) as never,
    };
  });
}

/** One page with a line of text. */
function pdf(): Buffer {
  const content = 'BT /F1 24 Tf 30 300 Td (Contratto) Tj ET';
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]/Contents 4 0 R/Resources<</Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>>>>>',
    `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
  ];
  let text = '%PDF-1.7\n';
  const offsets: number[] = [];
  bodies.forEach((body, i) => {
    offsets.push(text.length);
    text += `${i + 1} 0 obj\n${body}\nendobj\n`;
  });
  const xref = text.length;
  text += `xref\n0 ${bodies.length + 1}\n0000000000 65535 f \n`;
  text += offsets.map((o) => `${String(o).padStart(10, '0')} 00000 n \n`).join('');
  text += `trailer\n<</Size ${bodies.length + 1}/Root 1 0 R>>\nstartxref\n${xref}\n%%EOF\n`;
  return Buffer.from(text, 'latin1');
}

async function openDialog(page: Page) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'contratto.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await page.getByRole('button', { name: /^Firme/ }).click();
  await page.getByTestId('signatures-panel').getByRole('button', { name: 'Firma il documento…' }).click();
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('without a shell for it there is no smart card to choose', async ({ page }) => {
  await openDialog(page);
  await expect(page.getByRole('dialog')).toContainText('Firma invisibile');
  await expect(page.getByRole('dialog').getByText('Smart card')).toHaveCount(0);
});

test('signs with a smart card, time-stamped, for the long term; a wrong PIN is said', async ({ page }, testInfo) => {
  await withCard(page);
  await openDialog(page);
  const dialog = page.getByRole('dialog');
  await dialog.getByText('Smart card').click();
  await dialog.getByRole('button', { name: 'Cerca' }).click();
  // The card's one signing certificate, read by the engine (the CA's is its chain).
  await expect(dialog.getByTestId('sign-certificate')).toContainText('Luca Verdi (test)');
  await dialog.getByLabel("Indirizzo dell'autorità di marca temporale").fill('http://tsa.prova/');
  await dialog.getByLabel(/Validazione a lungo termine/).check();
  // What goes on the network is said before signing.
  await expect(dialog.getByTestId('sign-network')).toContainText('http://tsa.prova/');
  await expect(dialog.getByTestId('sign-network')).toContainText("solo l'impronta del documento");

  await dialog.getByLabel('PIN della carta').fill('0000');
  await dialog.getByRole('button', { name: 'Firma e salva' }).click();
  await expect(dialog.getByRole('alert')).toContainText('PIN errato');
  expect(asked).toEqual([]); // nothing asked of the network before the card signed

  await dialog.getByLabel('PIN della carta').fill('1234');
  const download = page.waitForEvent('download');
  await dialog.getByRole('button', { name: 'Firma e salva' }).click();
  const saved = readFileSync(await (await download).path());
  // Kept for verifiers outside the test (pyHanko, pdfsig: PLAN.md 11c's exit criterion).
  writeFileSync(testInfo.outputPath('signed-lta.pdf'), saved);
  const file = saved.toString('latin1');
  await expect(dialog).toBeHidden();
  // The signature's time stamp, the answers about the chains (the signer's, its authority's), the
  // document time stamp, the answers about its authority's chain (already there: not asked again).
  expect(asked).toEqual([
    'timestamp http://tsa.prova/',
    'ocsp http://127.0.0.1:9/ocsp',
    'ocsp http://127.0.0.1:9/ocsp',
    'timestamp http://tsa.prova/',
  ]);
  expect(file).toMatch(/\/SubFilter\s*\/ETSI\.CAdES\.detached/);
  expect(file).toMatch(/\/SubFilter\s*\/ETSI\.RFC3161/);
  expect(file).toMatch(/\/DSS\s/);

  const list = page.getByTestId('signature-list').getByRole('listitem');
  await expect(list).toHaveCount(2);
  await expect(list.nth(0)).toContainText('Luca Verdi (test)');
  await expect(list.nth(0)).toContainText('PAdES B-LTA');
  await expect(list.nth(0)).toContainText('Certificato non revocato (verifica conservata nel file)');
  await expect(list.nth(1)).toContainText('Marca temporale valida');
});
