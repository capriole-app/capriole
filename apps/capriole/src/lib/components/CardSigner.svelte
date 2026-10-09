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

  const editor = getEditor();
  const shell = cardShell();

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
      else if (tokens.length === 0) problem = 'Nessuna carta nei lettori: inseriscila e cerca di nuovo.';
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
      if (own.length === 0) problem = 'La carta non ha certificati di firma.';
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
    <span class="font-medium">Modulo PKCS#11 della carta</span>
    <span class="flex gap-2">
      <input class="{field} min-w-0 flex-1" list="pkcs11-modules" bind:value={module} spellcheck="false" />
      <button
        type="button"
        class="flex items-center gap-1.5 rounded-md border px-3 py-1.5 hover:bg-muted disabled:opacity-60"
        disabled={reading || !module.trim()}
        onclick={findCards}
      >
        <RefreshCw class="size-4" /> Cerca
      </button>
    </span>
    <datalist id="pkcs11-modules">
      {#each knownModules as known (known)}
        <option value={known}></option>
      {/each}
    </datalist>
    <span class="text-xs text-muted-foreground">La libreria del produttore della carta (OpenSC, CNS, CIE).</span>
  </label>

  {#if tokens && tokens.length > 1}
    <label class="flex flex-col gap-1">
      <span class="font-medium">Carta</span>
      <select class={field} onchange={(e) => readCard(Number(e.currentTarget.value))}>
        <option value="" selected={slot === null} disabled>Scegli la carta…</option>
        {#each tokens as token (token.slot)}
          <option value={token.slot} selected={slot === token.slot}
            >{token.label} · {token.manufacturer} {token.model}</option
          >
        {/each}
      </select>
    </label>
  {/if}

  {#if reading}
    <p class="text-muted-foreground" aria-live="polite">Lettura della carta…</p>
  {:else if problem}
    <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{problem}</p>
  {/if}

  {#if signing.length > 1}
    <fieldset class="flex flex-col gap-1">
      <legend class="mb-1 font-medium">Certificato</legend>
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
        ><CreditCard class="size-4" /> Firmerai come <strong>{certificate.info.name}</strong></span
      >
      <span class="text-xs text-muted-foreground">
        Emesso da {commonName(certificate.info.issuer)} · valido dal {localDateTime(certificate.info.notBefore)} al {localDateTime(
          certificate.info.notAfter,
        )} · {certificate.info.key}
      </span>
    </div>
    <input
      type="password"
      class={field}
      aria-label="PIN della carta"
      placeholder="PIN della carta"
      autocomplete="off"
      inputmode="numeric"
      bind:value={pin}
    />
  {/if}

  <label class="flex flex-col gap-1">
    <span class="font-medium">Marca temporale</span>
    <input
      class={field}
      bind:value={tsa}
      placeholder="https://… (facoltativa)"
      spellcheck="false"
      aria-label="Indirizzo dell'autorità di marca temporale"
      onchange={() => rememberTsa(tsa.trim())}
    />
  </label>
  <label class="flex items-center gap-2">
    <input type="checkbox" bind:checked={longTerm} />
    Validazione a lungo termine (aggiunge al file le verifiche di revoca dei certificati)
  </label>
  {#if network}
    <p class="rounded-md bg-muted px-3 py-2 text-xs" role="note" data-testid="sign-network">
      Capriole si collegherà a
      {#if tsa.trim()}<strong>{tsa.trim()}</strong> per la marca temporale (esce solo l'impronta del documento, non il documento){/if}{#if tsa.trim() && longTerm}
        e
      {/if}{#if longTerm}ai servizi di verifica (OCSP, CRL) indicati nei certificati, a cui va solo il numero di serie
        di ciascun certificato{/if}.
    </p>
  {/if}
</div>
