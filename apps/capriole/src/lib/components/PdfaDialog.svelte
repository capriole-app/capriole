<script lang="ts">
  // The conversion to PDF/A: what it is, then "Converti" — what was put right, or why it cannot be done —,
  // then the converted document saved as a new file. The conversion is one step of undo.
  import Archive from '@lucide/svelte/icons/archive';
  import CircleCheck from '@lucide/svelte/icons/circle-check';
  import FileDown from '@lucide/svelte/icons/file-down';
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import { getEditor } from '$lib/editor.svelte';
  import { pdfaChange, pdfaReason } from '$lib/pdfa';
  import { tr } from '$lib/i18n/index.svelte';
  import { pdfaWords } from '$lib/i18n/pdfa';
  import { dialog, dialogFooter, dialogTitle, primaryButton, secondaryButton } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(pdfaWords));
  const open = $derived(editor.pdfaDialog !== null);
  const invoice = $derived(editor.pdfaDialog === 3);
  const reasons = $derived([...new Set((editor.pdfaRefusal ?? []).map(pdfaReason))]);

  // A modal dialog while it is open (showModal: focus trapped, Escape closes).
  function modal(dialog: HTMLDialogElement) {
    if (open && !dialog.open) dialog.showModal();
    if (!open && dialog.open) dialog.close();
  }
</script>

<dialog
  {@attach modal}
  class={[dialog, 'max-w-lg']}
  aria-labelledby="pdfa-title"
  data-testid="pdfa-dialog"
  oncancel={(e) => {
    e.preventDefault();
    if (!editor.busy) editor.closePdfa();
  }}
>
  <div class="flex flex-col gap-4 text-sm">
    <h2 id="pdfa-title" class={dialogTitle}>
      <Archive class="size-4" />
      {t.title(invoice)}
    </h2>

    {#if editor.pdfaReport}
      {@const report = editor.pdfaReport}
      <p
        class="flex items-center gap-2 rounded-md bg-success-background px-3 py-2 font-medium text-success"
        role="status"
      >
        <CircleCheck class="size-4 shrink-0" />
        {t.done(report.part)}
      </p>
      {#if report.changes.length > 0}
        <div class="flex flex-col gap-1">
          <span class="font-medium">{t.changed}</span>
          <ul class="list-disc ps-5 text-muted-foreground" data-testid="pdfa-changes">
            {#each report.changes as change, i (i)}
              <li>{pdfaChange(change)}</li>
            {/each}
          </ul>
        </div>
      {/if}
      <p class="text-xs text-muted-foreground">
        {t.saveHint}
      </p>
    {:else if editor.pdfaRefusal}
      <div class="flex flex-col gap-2 rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">
        <span class="flex items-center gap-2 font-medium"><TriangleAlert class="size-4 shrink-0" /> {t.refused}</span>
        <ul class="list-disc ps-5" data-testid="pdfa-reasons">
          {#each reasons as reason (reason)}
            <li>{reason}</li>
          {/each}
        </ul>
      </div>
      <p class="text-xs text-muted-foreground">{t.unchanged}</p>
    {:else}
      <p class="text-muted-foreground">
        {t.about}
        {invoice ? t.aboutInvoice : t.aboutParts}
      </p>
      <p class="text-xs text-muted-foreground">
        {t.howItWorks}
      </p>
    {/if}

    {#if editor.error && open}
      <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{editor.error}</p>
    {/if}

    <div class={dialogFooter}>
      <button type="button" class={secondaryButton} disabled={editor.busy} onclick={() => editor.closePdfa()}
        >{editor.pdfaReport ? t.close : t.cancel}</button
      >
      {#if editor.pdfaReport}
        <button type="button" class={primaryButton} disabled={editor.busy} onclick={() => editor.savePdfa()}>
          <FileDown class="size-4" />
          {t.save}
        </button>
      {:else if !editor.pdfaRefusal}
        <button type="button" class={primaryButton} disabled={editor.busy} onclick={() => editor.convertToPdfa()}>
          <Archive class="size-4" />
          {t.convert}
        </button>
      {/if}
    </div>
  </div>
</dialog>
