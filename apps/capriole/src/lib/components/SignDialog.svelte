<script lang="ts">
  // The signature dialog: the key file (.p12, .pfx) and its password — its certificate read and shown
  // before signing —, the reason and place, and what signing does to the file. The key's bytes stay in
  // this dialog until it closes; nothing leaves the browser.
  import FileKey from '@lucide/svelte/icons/file-key';
  import Signature from '@lucide/svelte/icons/signature';
  import { EngineError, Status, type CertificateInfo } from '@capriole/engine-client';
  import { getEditor } from '$lib/editor.svelte';
  import { commonName } from '$lib/certificates';
  import { localDateTime } from '$lib/files';

  const editor = getEditor();

  let key = $state.raw<{ name: string; data: ArrayBuffer } | null>(null);
  let password = $state('');
  let certificate = $state.raw<CertificateInfo | null>(null);
  let problem = $state('');
  let checking = $state(false);
  let reason = $state('');
  let location = $state('');

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
          ? 'Password errata.'
          : `Il file non si legge come certificato con chiave (.p12, .pfx): ${error instanceof Error ? error.message : String(error)}`;
    } finally {
      checking = false;
    }
  }

  function close() {
    editor.signRequest = null;
    key = null;
    password = '';
    certificate = null;
    problem = '';
    reason = '';
    location = '';
  }

  async function sign() {
    if (!key || !certificate) return;
    const signed = await editor.sign(key.data.slice(0), password, { reason: reason.trim(), location: location.trim() });
    if (signed) close();
  }

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
      <Signature class="size-4" /> Firma digitale
    </h2>
    <p class="text-muted-foreground">
      {#if request?.rect}
        Firma visibile nel riquadro disegnato a pagina {request.page + 1}.
      {:else}
        Firma invisibile. Per una firma visibile, apri una pagina e disegna il riquadro con lo strumento «Firma».
      {/if}
    </p>

    <div class="flex flex-col gap-1.5">
      <span class="font-medium">Certificato con chiave privata</span>
      <label
        class="flex cursor-pointer items-center gap-2 rounded-md border border-dashed px-3 py-2 focus-within:ring-2 focus-within:ring-primary hover:bg-muted"
      >
        <FileKey class="size-4 shrink-0" />
        <span class="truncate">{key ? key.name : 'Scegli il file .p12 o .pfx…'}</span>
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
        aria-label="Password del certificato"
        placeholder="Password del certificato"
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
        <p class="text-muted-foreground" aria-live="polite">Lettura del certificato…</p>
      {:else if problem}
        <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{problem}</p>
      {:else if certificate}
        <div class="flex flex-col gap-0.5 rounded-md bg-muted px-3 py-2" data-testid="sign-certificate">
          <span>Firmerai come <strong>{certificate.name}</strong></span>
          <span class="text-xs text-muted-foreground">
            Emesso da {commonName(certificate.issuer)} · valido dal {localDateTime(certificate.notBefore)} al {localDateTime(
              certificate.notAfter,
            )} · {certificate.key}
          </span>
        </div>
        {#if expired || early}
          <p class="rounded-md bg-warning-background px-3 py-2 text-warning" role="note">
            {expired ? 'Il certificato è scaduto' : 'Il certificato non è ancora valido'}: la firma risulterà fatta con
            un certificato non valido in quel momento.
          </p>
        {/if}
      {/if}
    </div>

    <div class="grid grid-cols-2 gap-2">
      <label class="flex flex-col gap-1">
        <span class="font-medium">Motivo</span>
        <input class={field} bind:value={reason} placeholder="facoltativo" />
      </label>
      <label class="flex flex-col gap-1">
        <span class="font-medium">Luogo</span>
        <input class={field} bind:value={location} placeholder="facoltativo" />
      </label>
    </div>

    <p class="text-xs text-muted-foreground">
      La firma diventa una nuova versione del file, che viene salvato subito (con «-firmato» nel nome). Le modifiche
      fatte finora ne fanno parte; quelle successive si aggiungono dopo senza invalidarla, ma risulteranno fatte dopo la
      firma. La chiave resta nel browser.
    </p>

    {#if editor.error && request}
      <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{editor.error}</p>
    {/if}

    <div class="flex justify-end gap-2">
      <button type="button" class="rounded-md border px-3 py-1.5 hover:bg-muted" disabled={editor.busy} onclick={close}
        >Annulla</button
      >
      <button
        type="submit"
        class="flex items-center gap-1.5 rounded-md bg-primary px-3 py-1.5 font-medium text-primary-foreground disabled:opacity-60"
        disabled={!certificate || editor.busy || checking}
      >
        <Signature class="size-4" /> Firma e salva
      </button>
    </div>
  </form>
</dialog>
