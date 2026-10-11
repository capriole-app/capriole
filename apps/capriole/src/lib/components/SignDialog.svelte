<script lang="ts">
  // The signature dialog: the key file (.p12, .pfx) and its password — its certificate read and shown
  // before signing —, the reason and place, and what signing does to the file. The key's bytes stay in
  // this dialog until it closes; nothing leaves the browser. On the desktop, a smart card instead
  // (CardSigner): then a time stamp and the long-term validation data can be asked for, and what could
  // not be got is said before the dialog closes.
  import FileKey from '@lucide/svelte/icons/file-key';
  import Signature from '@lucide/svelte/icons/signature';
  import { EngineError, Status, type CertificateInfo } from '@capriole/engine-client';
  import { getEditor } from '$lib/editor.svelte';
  import { commonName } from '$lib/certificates';
  import { localDateTime } from '$lib/files';
  import { cardShell, type CardSigner as Card } from '$lib/smartcard';
  import CardSigner from './CardSigner.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { signingWords } from '$lib/i18n/signing';

  const editor = getEditor();
  const t = $derived(tr(signingWords));

  let key = $state.raw<{ name: string; data: ArrayBuffer } | null>(null);
  let password = $state('');
  let certificate = $state.raw<CertificateInfo | null>(null);
  let problem = $state('');
  let checking = $state(false);
  let reason = $state('');
  let location = $state('');
  // With a smart card (only where there is a shell for it).
  const cards = cardShell() !== null;
  let method = $state<'file' | 'card'>('file');
  let cardPart = $state<ReturnType<typeof CardSigner>>();
  const card = $derived<Card | null>(method === 'card' ? (cardPart?.current() ?? null) : null);
  // A card signature made, with what the long term could not get: said before closing.
  let incomplete = $state(false);

  const request = $derived(editor.signRequest);
  // Against now, when the certificate is read.
  const expired = $derived(certificate !== null && moment(certificate.notAfter) < new Date());
  const early = $derived(certificate !== null && moment(certificate.notBefore) > new Date());

  // A modal dialog while a signature is being made (showModal: focus trapped, Escape cancels).
  function modal(dialog: HTMLDialogElement) {
    if (request && !dialog.open) dialog.showModal();
    if (!request && dialog.open) dialog.close();
  }

  /** A certificate's date (a PDF date, UTC) as a moment. */
  function moment(pdf: string): Date {
    const m = /^D:(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})/.exec(pdf);
    return m ? new Date(Date.UTC(+m[1]!, +m[2]! - 1, +m[3]!, +m[4]!, +m[5]!, +m[6]!)) : new Date(NaN);
  }

  /** Reads the certificate of the key file with the password (a copy goes to the engine). */
  async function check() {
    certificate = null;
    problem = '';
    if (!key) return;
    checking = true;
    try {
      certificate = await editor.engine.pkcs12Info(key.data.slice(0), password);
    } catch (error) {
      problem =
        error instanceof EngineError && error.status === Status.passwordRequired
          ? t.wrongPassword
          : t.notAKey(error instanceof Error ? error.message : String(error));
    } finally {
      checking = false;
    }
  }

  function close() {
    editor.signRequest = null;
    incomplete = false;
    method = 'file'; // the card part goes, and its PIN with it
    key = null;
    password = '';
    certificate = null;
    problem = '';
    reason = '';
    location = '';
  }

  async function sign() {
    const details = { reason: reason.trim(), location: location.trim() };
    if (method === 'card') {
      if (!card) return;
      const signed = await editor.signWithCard(card, details);
      const report = editor.longTermReport;
      if (signed && report && (report.unanswered.length > 0 || report.documentTimestamp)) incomplete = true;
      else if (signed) close();
      return;
    }
    if (!key || !certificate) return;
    const signed = await editor.sign(key.data.slice(0), password, details);
    if (signed) close();
  }

  const ready = $derived(method === 'card' ? card !== null : certificate !== null);

  const field = 'rounded-md border bg-background px-3 py-2 text-base';
</script>

<dialog
  {@attach modal}
  class="m-auto w-full max-w-md rounded-xl border bg-card p-6 text-foreground shadow-lg backdrop:bg-black/40"
  aria-labelledby="sign-title"
  oncancel={(e) => {
    e.preventDefault();
    if (!editor.busy) close();
  }}
>
  <form
    class="flex flex-col gap-4 text-sm"
    onsubmit={(e) => {
      e.preventDefault();
      sign();
    }}
  >
    <h2 id="sign-title" class="flex items-center gap-2 text-base font-semibold">
      <Signature class="size-4" />
      {t.title}
    </h2>
    <p class="text-muted-foreground">
      {request?.rect ? t.visible(request.page + 1) : t.invisible}
    </p>

    {#if incomplete}
      <div
        class="flex flex-col gap-2 rounded-md bg-warning-background px-3 py-2 text-warning"
        role="status"
        data-testid="sign-incomplete"
      >
        <p>{t.incomplete}</p>
        <ul class="list-disc pl-5">
          {#each editor.longTermReport?.unanswered ?? [] as subject (subject)}
            <li>{t.noRevocationAnswer(subject)}</li>
          {/each}
          {#if editor.longTermReport?.documentTimestamp}
            <li>{t.noDocumentTimestamp(editor.longTermReport.documentTimestamp)}</li>
          {/if}
        </ul>
      </div>
      <div class="flex justify-end">
        <button type="button" class="rounded-md border px-3 py-1.5 hover:bg-muted" onclick={close}>{t.close}</button>
      </div>
    {:else}
      {#if cards}
        <div class="flex gap-1 rounded-md border p-1" role="radiogroup" aria-label={t.method}>
          {#each [['file', t.methodFile], ['card', t.methodCard]] as [value, label] (value)}
            <label
              class={[
                'flex-1 cursor-pointer rounded px-2 py-1 text-center',
                method === value && 'bg-muted font-medium',
              ]}
            >
              <input type="radio" class="sr-only" name="sign-method" {value} bind:group={method} />
              {label}
            </label>
          {/each}
        </div>
      {/if}

      {#if method === 'card'}
        <CardSigner bind:this={cardPart} />
      {:else}
        <div class="flex flex-col gap-1.5">
          <span class="font-medium">{t.keyFile}</span>
          <label
            class="flex cursor-pointer items-center gap-2 rounded-md border border-dashed px-3 py-2 focus-within:ring-2 focus-within:ring-primary hover:bg-muted"
          >
            <FileKey class="size-4 shrink-0" />
            <span class="truncate">{key ? key.name : t.chooseKey}</span>
            <input
              type="file"
              accept=".p12,.pfx,application/x-pkcs12"
              class="sr-only"
              data-testid="sign-key"
              onchange={async (e) => {
                const file = e.currentTarget.files?.[0];
                e.currentTarget.value = '';
                if (!file) return;
                key = { name: file.name, data: await file.arrayBuffer() };
                await check();
              }}
            />
          </label>
          <input
            type="password"
            class={field}
            aria-label={t.keyPassword}
            placeholder={t.keyPassword}
            autocomplete="off"
            bind:value={password}
            onchange={check}
            onkeydown={(e) => {
              if (e.key === 'Enter' && !certificate) {
                e.preventDefault();
                check();
              }
            }}
          />
          {#if checking}
            <p class="text-muted-foreground" aria-live="polite">{t.readingCertificate}</p>
          {:else if problem}
            <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{problem}</p>
          {:else if certificate}
            <div class="flex flex-col gap-0.5 rounded-md bg-muted px-3 py-2" data-testid="sign-certificate">
              <span>{t.signAs} <strong>{certificate.name}</strong></span>
              <span class="text-xs text-muted-foreground">
                {t.issued(
                  commonName(certificate.issuer),
                  localDateTime(certificate.notBefore),
                  localDateTime(certificate.notAfter),
                  certificate.key,
                )}
              </span>
            </div>
            {#if expired || early}
              <p class="rounded-md bg-warning-background px-3 py-2 text-warning" role="note">
                {t.notValidNow(expired)}
              </p>
            {/if}
          {/if}
        </div>
      {/if}

      <div class="grid grid-cols-2 gap-2">
        <label class="flex flex-col gap-1">
          <span class="font-medium">{t.reason}</span>
          <input class={field} bind:value={reason} placeholder={t.optional} />
        </label>
        <label class="flex flex-col gap-1">
          <span class="font-medium">{t.location}</span>
          <input class={field} bind:value={location} placeholder={t.optional} />
        </label>
      </div>

      <p class="text-xs text-muted-foreground">
        {t.whatSigningDoes}
        {method === 'card' ? t.keyStaysOnCard : t.keyStaysInBrowser}
      </p>

      {#if editor.error && request}
        <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{editor.error}</p>
      {/if}

      <div class="flex justify-end gap-2">
        <button
          type="button"
          class="rounded-md border px-3 py-1.5 hover:bg-muted"
          disabled={editor.busy}
          onclick={close}>{t.cancel}</button
        >
        <button
          type="submit"
          class="flex items-center gap-1.5 rounded-md bg-primary px-3 py-1.5 font-medium text-primary-foreground disabled:opacity-60"
          disabled={!ready || editor.busy || checking}
        >
          <Signature class="size-4" />
          {t.signAndSave}
        </button>
      </div>
    {/if}
  </form>
</dialog>
