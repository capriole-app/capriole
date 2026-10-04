<script lang="ts">
  // Beside the pages: the document's attachments — downloaded, added (chosen or dropped), removed — and,
  // when it carries one, its Factur-X invoice summarised from its XML; or the way to make it one.
  import Download from '@lucide/svelte/icons/download';
  import FileCode from '@lucide/svelte/icons/file-code';
  import Paperclip from '@lucide/svelte/icons/paperclip';
  import Plus from '@lucide/svelte/icons/plus';
  import ReceiptText from '@lucide/svelte/icons/receipt-text';
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import X from '@lucide/svelte/icons/x';
  import { getEditor } from '$lib/editor.svelte';
  import { formatBytes, readableDate } from '$lib/files';

  const editor = getEditor();
  let files: HTMLInputElement | undefined = $state();
  let xml: HTMLInputElement | undefined = $state();
  let dragging = $state(false);

  const relationships: Record<string, string> = {
    Source: 'sorgente',
    Data: 'dati',
    Alternative: 'versione alternativa',
    Supplement: 'supplemento',
    EncryptedPayload: 'contenuto cifrato',
    FormData: 'dati del modulo',
    Schema: 'schema',
  };
  const kinds: Record<string, string> = {
    '380': 'Fattura',
    '381': 'Nota di credito',
    '384': 'Fattura correttiva',
    '389': 'Autofattura',
  };

  function amount(value: string, currency: string): string {
    const n = Number(value);
    if (value === '' || Number.isNaN(n)) return value;
    try {
      return new Intl.NumberFormat('it-IT', { style: 'currency', currency: currency || 'EUR' }).format(n);
    } catch {
      return `${value} ${currency}`;
    }
  }

  function date(iso: string): string {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso);
    return m ? `${m[3]}/${m[2]}/${m[1]}` : iso;
  }

  const button =
    'flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm hover:bg-muted disabled:pointer-events-none disabled:opacity-50';
</script>

<aside
  class={[
    'flex w-80 shrink-0 flex-col gap-4 overflow-y-auto border-s bg-card p-4 text-sm',
    dragging && 'ring-2 ring-primary ring-inset',
  ]}
  aria-label="Allegati"
  data-testid="attachments-panel"
  ondragover={(e) => {
    e.preventDefault();
    dragging = true;
  }}
  ondragleave={() => (dragging = false)}
  ondrop={(e) => {
    e.preventDefault();
    dragging = false;
    editor.addAttachments([...(e.dataTransfer?.files ?? [])]);
  }}
>
  <header class="flex items-center justify-between">
    <h2 class="flex items-center gap-2 font-semibold"><Paperclip class="size-4" /> Allegati</h2>
    <button
      type="button"
      class="flex size-8 items-center justify-center rounded-md hover:bg-muted"
      aria-label="Chiudi gli allegati"
      onclick={() => (editor.attachmentsOpen = false)}><X class="size-4" /></button
    >
  </header>

  {#if editor.facturx}
    <section class="flex flex-col gap-2 rounded-lg border bg-background p-3" data-testid="invoice">
      <h3 class="flex items-center gap-2 font-semibold">
        <ReceiptText class="size-4" /> Fattura elettronica Factur-X
      </h3>
      <p class="text-muted-foreground">
        Profilo {editor.facturx.conformanceLevel || 'non indicato'}{editor.facturx.version
          ? ` · versione ${editor.facturx.version}`
          : ''}
      </p>
      {#if editor.invoice}
        {@const i = editor.invoice}
        <dl class="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1">
          <dt class="text-muted-foreground">{kinds[i.typeCode] ?? 'Documento'}</dt>
          <dd>{i.number}{i.date ? ` del ${date(i.date)}` : ''}</dd>
          <dt class="text-muted-foreground">Cedente</dt>
          <dd>{i.seller}{i.sellerVat ? ` (${i.sellerVat})` : ''}</dd>
          <dt class="text-muted-foreground">Cessionario</dt>
          <dd>{i.buyer}</dd>
          {#if i.net}
            <dt class="text-muted-foreground">Imponibile</dt>
            <dd>{amount(i.net, i.currency)}</dd>
          {/if}
          {#if i.tax}
            <dt class="text-muted-foreground">Imposta</dt>
            <dd>{amount(i.tax, i.currency)}</dd>
          {/if}
          <dt class="text-muted-foreground">Totale</dt>
          <dd class="font-semibold">{amount(i.total, i.currency)}</dd>
          {#if i.due && i.due !== i.total}
            <dt class="text-muted-foreground">Da pagare</dt>
            <dd>{amount(i.due, i.currency)}</dd>
          {/if}
          {#if i.dueDate}
            <dt class="text-muted-foreground">Scadenza</dt>
            <dd>{date(i.dueDate)}</dd>
          {/if}
        </dl>
      {:else}
        <p class="text-muted-foreground">L'XML della fattura non si legge come un Cross Industry Invoice.</p>
      {/if}
      {#if !editor.facturx.pdfa3}
        <div class="flex flex-col gap-2 rounded-md bg-warning-background px-2 py-1.5 text-warning" role="note">
          <p>
            Il PDF non è PDF/A-3: la fattura è incorporata, ma il file diventa un Factur-X conforme solo convertito in
            PDF/A-3.
          </p>
          <button
            type="button"
            class={[button, 'self-start bg-card text-foreground']}
            disabled={editor.busy}
            onclick={() => editor.openPdfa(3)}
          >
            Converti in PDF/A-3…
          </button>
        </div>
      {/if}
    </section>
  {:else}
    <section class="flex flex-col gap-2">
      <p class="text-muted-foreground">
        Una fattura elettronica Factur-X / ZUGFeRD è un PDF con l'XML della fattura (Cross Industry Invoice) dentro.
      </p>
      <button type="button" class={[button, 'self-start']} disabled={editor.busy} onclick={() => xml?.click()}>
        <FileCode class="size-4" /> Crea Factur-X con un XML…
      </button>
      <input
        bind:this={xml}
        type="file"
        accept=".xml,text/xml,application/xml"
        class="hidden"
        data-testid="invoice-file"
        onchange={(e) => {
          const file = e.currentTarget.files?.[0];
          if (file) editor.embedInvoice(file);
          e.currentTarget.value = '';
        }}
      />
    </section>
  {/if}

  <section class="flex flex-col gap-2">
    {#if editor.attachments.length === 0}
      <p class="text-muted-foreground">Nessun file allegato.</p>
    {/if}
    <ul class="flex flex-col gap-2" data-testid="attachment-list">
      {#each editor.attachments as a (`${a.ref[0]} ${a.ref[1]}`)}
        <li class="flex flex-col gap-1 rounded-md border p-2">
          <span class="truncate font-medium" title={a.fileName || a.name}>{a.fileName || a.name}</span>
          <span class="text-xs text-muted-foreground">
            {[
              a.size !== null ? formatBytes(a.size) : '',
              relationships[a.relationship] ?? '',
              a.page !== null ? `nota a pagina ${a.page + 1}` : '',
              readableDate(a.modificationDate),
            ]
              .filter(Boolean)
              .join(' · ')}
          </span>
          {#if a.description && a.description !== (a.fileName || a.name)}
            <span class="text-xs">{a.description}</span>
          {/if}
          <div class="flex gap-1.5">
            <button type="button" class={button} disabled={editor.busy} onclick={() => editor.saveAttachment(a)}>
              <Download class="size-4" /> Scarica
            </button>
            {#if a.page === null}
              <button
                type="button"
                class={[button, 'text-danger hover:bg-danger-background']}
                disabled={editor.busy}
                aria-label="Rimuovi {a.fileName || a.name}"
                onclick={() => editor.removeAttachment(a.ref)}><Trash2 class="size-4" /> Rimuovi</button
              >
            {/if}
          </div>
        </li>
      {/each}
    </ul>
    <button type="button" class={[button, 'self-start']} disabled={editor.busy} onclick={() => files?.click()}>
      <Plus class="size-4" /> Aggiungi file
    </button>
    <input
      bind:this={files}
      type="file"
      multiple
      class="hidden"
      data-testid="attach-file"
      onchange={(e) => {
        editor.addAttachments([...(e.currentTarget.files ?? [])]);
        e.currentTarget.value = '';
      }}
    />
    <p class="text-xs text-muted-foreground">…oppure trascina qui i file da allegare.</p>
  </section>
</aside>
