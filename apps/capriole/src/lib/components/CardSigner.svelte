<script lang="ts">
  // The smart card part of the signature dialog (desktop, PLAN.md phase 11c): the card's PKCS#11 module
  // (remembered), the card, its certificate — read by the engine and shown before signing —, the PIN,
  // and what goes on the network: a time-stamping authority, and the long-term validation data. The
  // PIN stays in this component and the shell; the key never leaves the card.
  import CreditCard from '@lucide/svelte/icons/credit-card';
  import RefreshCw from '@lucide/svelte/icons/refresh-cw';
  import type { CertificateInfo } from '@capriole/engine-client';
  import { getEditor } from '$lib/editor.svelte';
  import { commonName } from '$lib/certificates';
  import { localDateTime } from '$lib/files';
  import {
    cardProblem,
    cardShell,
    knownModules,
    rememberModule,
    rememberTsa,
    rememberedModule,
    rememberedTsa,
    type CardCertificate,
    type CardSigner,
    type Token,
  } from '$lib/smartcard';
  import { tr } from '$lib/i18n/index.svelte';
  import { signingWords } from '$lib/i18n/signing';

  const editor = getEditor();
  const shell = cardShell();
  const t = $derived(tr(signingWords));

  let module = $state(rememberedModule() || knownModules[0]!);
  let tokens = $state.raw<Token[] | null>(null);
  let slot = $state<number | null>(null);
  let certificates = $state.raw<{ card: CardCertificate; info: CertificateInfo }[]>([]);
  let chosen = $state<string | null>(null);
  let pin = $state('');
  let tsa = $state(rememberedTsa());
  let longTerm = $state(false);
  let problem = $state('');
  let reading = $state(false);

  const certificate = $derived(certificates.find((c) => c.card.id === chosen) ?? null);
  // What may sign: the certificates that are not an authority's (those are the chain).
  const signing = $derived(certificates.filter((c) => !c.info.ca));
  const network = $derived(tsa.trim() !== '' || longTerm);

  const signer = $derived<CardSigner | null>(
    certificate && pin && slot !== null
      ? {
          module,
          slot,
          certificate: certificate.card,
          chain: certificates.filter((c) => c !== certificate).map((c) => c.card.der),
          pin,
          tsa: tsa.trim(),
          longTerm,
        }
      : null,
  );

  /** The signature the person chose, once card, certificate and PIN are given (else null). */
  export function current(): CardSigner | null {
    return signer;
  }

  async function findCards() {
    if (!shell) return;
    problem = '';
    tokens = null;
    slot = null;
    certificates = [];
    chosen = null;
    reading = true;
    try {
      tokens = await shell.tokens(module.trim());
      rememberModule(module.trim());
      if (tokens.length === 1) await readCard(tokens[0]!.slot);
      else if (tokens.length === 0) problem = t.noCards;
    } catch (error) {
      problem = cardProblem(error);
    } finally {
      reading = false;
    }
  }

  async function readCard(id: number) {
    if (!shell) return;
    slot = id;
    certificates = [];
    chosen = null;
    problem = '';
    reading = true;
    try {
      const found = await shell.certificates(module.trim(), id);
      const read = [];
      for (const card of found) {
        try {
          read.push({ card, info: await editor.engine.certificateInfo(card.der) });
        } catch {
          // not a certificate the engine reads: not offered
        }
      }
      certificates = read;
      const own = read.filter((c) => !c.info.ca);
      if (own.length === 1) chosen = own[0]!.card.id;
      if (own.length === 0) problem = t.noSigningCertificates;
    } catch (error) {
      problem = cardProblem(error);
    } finally {
      reading = false;
    }
  }

  const field = 'rounded-md border bg-background px-3 py-2 text-base';
</script>

<div class="flex flex-col gap-3" data-testid="card-signer">
  <label class="flex flex-col gap-1">
    <span class="font-medium">{t.module}</span>
    <span class="flex gap-2">
      <input class="{field} min-w-0 flex-1" list="pkcs11-modules" bind:value={module} spellcheck="false" />
      <button
        type="button"
        class="flex items-center gap-1.5 rounded-md border px-3 py-1.5 hover:bg-muted disabled:opacity-60"
        disabled={reading || !module.trim()}
        onclick={findCards}
      >
        <RefreshCw class="size-4" />
        {t.find}
      </button>
    </span>
    <datalist id="pkcs11-modules">
      {#each knownModules as known (known)}
        <option value={known}></option>
      {/each}
    </datalist>
    <span class="text-xs text-muted-foreground">{t.moduleHint}</span>
  </label>

  {#if tokens && tokens.length > 1}
    <label class="flex flex-col gap-1">
      <span class="font-medium">{t.card}</span>
      <select class={field} onchange={(e) => readCard(Number(e.currentTarget.value))}>
        <option value="" selected={slot === null} disabled>{t.chooseCard}</option>
        {#each tokens as token (token.slot)}
          <option value={token.slot} selected={slot === token.slot}
            >{token.label} · {token.manufacturer} {token.model}</option
          >
        {/each}
      </select>
    </label>
  {/if}

  {#if reading}
    <p class="text-muted-foreground" aria-live="polite">{t.readingCard}</p>
  {:else if problem}
    <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{problem}</p>
  {/if}

  {#if signing.length > 1}
    <fieldset class="flex flex-col gap-1">
      <legend class="mb-1 font-medium">{t.certificate}</legend>
      {#each signing as c (c.card.id)}
        <label class="flex items-center gap-2">
          <input type="radio" name="card-certificate" value={c.card.id} bind:group={chosen} />
          {c.info.name} <span class="text-xs text-muted-foreground">({c.card.label})</span>
        </label>
      {/each}
    </fieldset>
  {/if}
  {#if certificate}
    <div class="flex flex-col gap-0.5 rounded-md bg-muted px-3 py-2" data-testid="sign-certificate">
      <span class="flex items-center gap-1.5"
        ><CreditCard class="size-4" /> {t.signAs} <strong>{certificate.info.name}</strong></span
      >
      <span class="text-xs text-muted-foreground">
        {t.issued(
          commonName(certificate.info.issuer),
          localDateTime(certificate.info.notBefore),
          localDateTime(certificate.info.notAfter),
          certificate.info.key,
        )}
      </span>
    </div>
    <input
      type="password"
      class={field}
      aria-label={t.pin}
      placeholder={t.pin}
      autocomplete="off"
      inputmode="numeric"
      bind:value={pin}
    />
  {/if}

  <label class="flex flex-col gap-1">
    <span class="font-medium">{t.timestamp}</span>
    <input
      class={field}
      bind:value={tsa}
      placeholder={t.tsaPlaceholder}
      spellcheck="false"
      aria-label={t.tsaAddress}
      onchange={() => rememberTsa(tsa.trim())}
    />
  </label>
  <label class="flex items-center gap-2">
    <input type="checkbox" bind:checked={longTerm} />
    {t.longTerm}
  </label>
  {#if network}
    <p class="rounded-md bg-muted px-3 py-2 text-xs" role="note" data-testid="sign-network">
      {#if tsa.trim()}
        {t.networkTo} <strong>{tsa.trim()}</strong>
        {t.networkTsa}{#if longTerm}
          {t.networkAnd} {t.networkChecks}{/if}.
      {:else}
        {t.networkChecksOnly}.
      {/if}
    </p>
  {/if}
</div>
