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
  import { i18n, tr } from '$lib/i18n/index.svelte';
  import { attachmentsWords } from '$lib/i18n/attachments';

  const editor = getEditor();
  let files: HTMLInputElement | undefined = $state();
  let xml: HTMLInputElement | undefined = $state();
  let dragging = $state(false);

  const t = $derived(tr(attachmentsWords));

  function amount(value: string, currency: string): string {
    const n = Number(value);
    if (value === '' || Number.isNaN(n)) return value;
    try {
      return new Intl.NumberFormat(i18n.intl, { style: 'currency', currency: currency || 'EUR' }).format(n);
    } catch {
      return `${value} ${currency}`;
    }
  }

  /** An ISO date (YYYY-MM-DD) as the language writes it; else as written. */
  function date(iso: string): string {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso);
    if (!m) return iso;
    const day = new Date(Date.UTC(+m[1]!, +m[2]! - 1, +m[3]!));
    return new Intl.DateTimeFormat(i18n.intl, {
      day: '2-digit',
      month: '2-digit',
      year: 'numeric',
      timeZone: 'UTC',
    }).format(day);
  }

  const button =
    'flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm hover:bg-muted disabled:pointer-events-none disabled:opacity-50';
</script>

<aside
  class={[
    'flex w-80 shrink-0 flex-col gap-4 overflow-y-auto border-s bg-card p-4 text-sm',
    dragging && 'ring-2 ring-emphasis ring-inset',
  ]}
  aria-label={t.title}
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
    <h2 class="flex items-center gap-2 font-semibold"><Paperclip class="size-4" /> {t.title}</h2>
    <button
      type="button"
      class="flex size-8 items-center justify-center rounded-md hover:bg-muted"
      aria-label={t.close}
      onclick={() => (editor.attachmentsOpen = false)}><X class="size-4" /></button
    >
  </header>

  {#if editor.facturx}
    <section class="flex flex-col gap-2 rounded-lg border bg-background p-3" data-testid="invoice">
      <h3 class="flex items-center gap-2 font-semibold">
        <ReceiptText class="size-4" />
        {t.invoice}
      </h3>
      <p class="text-muted-foreground">
        {t.profile(editor.facturx.conformanceLevel, editor.facturx.version)}
      </p>
      {#if editor.invoice}
        {@const i = editor.invoice}
        <dl class="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1">
          <dt class="text-muted-foreground">{t.kinds[i.typeCode] ?? t.document}</dt>
          <dd>{t.numberAndDate(i.number, i.date ? date(i.date) : '')}</dd>
          <dt class="text-muted-foreground">{t.seller}</dt>
          <dd>{i.seller}{i.sellerVat ? ` (${i.sellerVat})` : ''}</dd>
          <dt class="text-muted-foreground">{t.buyer}</dt>
          <dd>{i.buyer}</dd>
          {#if i.net}
            <dt class="text-muted-foreground">{t.net}</dt>
            <dd>{amount(i.net, i.currency)}</dd>
          {/if}
          {#if i.tax}
            <dt class="text-muted-foreground">{t.tax}</dt>
            <dd>{amount(i.tax, i.currency)}</dd>
          {/if}
          <dt class="text-muted-foreground">{t.total}</dt>
          <dd class="font-semibold">{amount(i.total, i.currency)}</dd>
          {#if i.due && i.due !== i.total}
            <dt class="text-muted-foreground">{t.due}</dt>
            <dd>{amount(i.due, i.currency)}</dd>
          {/if}
          {#if i.dueDate}
            <dt class="text-muted-foreground">{t.dueDate}</dt>
            <dd>{date(i.dueDate)}</dd>
          {/if}
        </dl>
      {:else}
        <p class="text-muted-foreground">{t.unreadable}</p>
      {/if}
      {#if !editor.facturx.pdfa3}
        <div class="flex flex-col gap-2 rounded-md bg-warning-background px-2 py-1.5 text-warning" role="note">
          <p>{t.notPdfa3}</p>
          <button
            type="button"
            class={[button, 'self-start bg-card text-foreground']}
            disabled={editor.busy}
            onclick={() => editor.openPdfa(3)}
          >
            {t.convert}
          </button>
        </div>
      {/if}
    </section>
  {:else}
    <section class="flex flex-col gap-2">
      <p class="text-muted-foreground">
        {t.aboutFacturx}
      </p>
      <button type="button" class={[button, 'self-start']} disabled={editor.busy} onclick={() => xml?.click()}>
        <FileCode class="size-4" />
        {t.makeFacturx}
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
      <p class="text-muted-foreground">{t.none}</p>
    {/if}
    <ul class="flex flex-col gap-2" data-testid="attachment-list">
      {#each editor.attachments as a (`${a.ref[0]} ${a.ref[1]}`)}
        <li class="flex flex-col gap-1 rounded-md border p-2">
          <span class="truncate font-medium" title={a.fileName || a.name}>{a.fileName || a.name}</span>
          <span class="text-xs text-muted-foreground">
            {[
              a.size !== null ? formatBytes(a.size) : '',
              t.relationships[a.relationship] ?? '',
              a.page !== null ? t.noteOnPage(a.page + 1) : '',
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
              <Download class="size-4" />
              {t.download}
            </button>
            {#if a.page === null}
              <button
                type="button"
                class={[button, 'text-danger hover:bg-danger-background']}
                disabled={editor.busy}
                aria-label={t.removeNamed(a.fileName || a.name)}
                onclick={() => editor.removeAttachment(a.ref)}><Trash2 class="size-4" /> {t.remove}</button
              >
            {/if}
          </div>
        </li>
      {/each}
    </ul>
    <button type="button" class={[button, 'self-start']} disabled={editor.busy} onclick={() => files?.click()}>
      <Plus class="size-4" />
      {t.add}
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
    <p class="text-xs text-muted-foreground">{t.dropHint}</p>
  </section>
</aside>
