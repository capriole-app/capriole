<script lang="ts">
  // The open file and what can be done to its pages. Actions on pages apply to the selection.
  import Archive from '@lucide/svelte/icons/archive';
  import ArrowLeft from '@lucide/svelte/icons/arrow-left';
  import ArrowRight from '@lucide/svelte/icons/arrow-right';
  import Copy from '@lucide/svelte/icons/copy';
  import FileDown from '@lucide/svelte/icons/file-down';
  import FileOutput from '@lucide/svelte/icons/file-output';
  import FilePlus from '@lucide/svelte/icons/file-plus';
  import Highlighter from '@lucide/svelte/icons/highlighter';
  import Paperclip from '@lucide/svelte/icons/paperclip';
  import ReceiptText from '@lucide/svelte/icons/receipt-text';
  import RotateCcw from '@lucide/svelte/icons/rotate-ccw';
  import RotateCw from '@lucide/svelte/icons/rotate-cw';
  import Signature from '@lucide/svelte/icons/signature';
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import Undo2 from '@lucide/svelte/icons/undo-2';
  import X from '@lucide/svelte/icons/x';
  import BrandMark from './BrandMark.svelte';
  import MoreMenu from './MoreMenu.svelte';
  import ZoomControl from './ZoomControl.svelte';
  import { gridRange } from '$lib/zoom';
  import { getEditor } from '$lib/editor.svelte';
  import { choosesWhere, fromBrowser } from '$lib/platform';
  import { tr } from '$lib/i18n/index.svelte';
  import { toolbarWords } from '$lib/i18n/toolbar';
  import { barButton as button, pressed, primaryButton, separator } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(toolbarWords));
  let input: HTMLInputElement | undefined = $state();

  const some = $derived(editor.selected.size > 0);
  const all = $derived(editor.selected.size === editor.pages);
</script>

<header class="flex flex-wrap items-center gap-x-3 gap-y-2 border-b bg-background px-4 py-2">
  <BrandMark label={t.about} />
  <div class="me-2 min-w-0 flex-1">
    <p class="truncate text-sm font-medium" title={editor.name ?? ''}>
      {editor.name}{#if editor.modified}<span class="text-emphasis"> •</span>{/if}
    </p>
    <!-- One line, truncated: the bar keeps its height whatever the selection says (a drag must not move the grid). -->
    <p class="truncate text-xs text-muted-foreground" aria-live="polite">
      {t.status(editor.pages, editor.selected.size)}
      {#if editor.busy}{t.working}{/if}
    </p>
  </div>

  <div class="flex flex-wrap items-center gap-1" role="toolbar" aria-label={t.selectedPages}>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.rotate(-90)}
      title={t.rotateLeft}
    >
      <RotateCcw class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.rotateLeft}</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.rotate(90)}
      title={t.rotateRight}
    >
      <RotateCw class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.rotateRight}</span>
    </button>
    <span class={separator} aria-hidden="true"></span>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.shift(-1)}
      aria-label={t.moveBefore}
      title={t.moveBefore}
    >
      <ArrowLeft class="size-4" />
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.shift(1)}
      aria-label={t.moveAfter}
      title={t.moveAfter}
    >
      <ArrowRight class="size-4" />
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.duplicate()}
      title={t.duplicate}
    >
      <Copy class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.duplicate}</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || all || editor.busy}
      title={all ? t.keepOne : undefined}
      onclick={() => editor.remove()}
    >
      <Trash2 class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.delete}</span>
    </button>
    <span class={separator} aria-hidden="true"></span>
    <button
      type="button"
      class={button}
      disabled={editor.selected.size !== 1 || editor.busy}
      title={t.annotateTitle}
      onclick={() => editor.view(editor.selection[0]!)}
    >
      <Highlighter class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.annotate}</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.extract()}
      title={t.extract}
    >
      <FileOutput class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.extract}</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={editor.busy}
      onclick={() => (choosesWhere ? editor.insertFromDialog() : input?.click())}
      title={t.insert}
    >
      <FilePlus class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.insert}</span>
    </button>
    <input
      bind:this={input}
      type="file"
      accept="application/pdf,.pdf"
      class="hidden"
      data-testid="insert-file"
      onchange={(e) => {
        const file = e.currentTarget.files?.[0];
        if (file) fromBrowser(file).then((picked) => editor.insert(picked));
        e.currentTarget.value = '';
      }}
    />
    <span class={separator} aria-hidden="true"></span>
    <button
      type="button"
      class={[button, editor.attachmentsOpen && pressed]}
      aria-pressed={editor.attachmentsOpen}
      title={editor.facturx ? t.invoiceTitle : t.attachmentsTitle}
      onclick={() => {
        editor.attachmentsOpen = !editor.attachmentsOpen;
        if (editor.attachmentsOpen) editor.signaturesOpen = false;
      }}
    >
      {#if editor.facturx}
        <ReceiptText class="size-4" />
      {:else}
        <Paperclip class="size-4" />
      {/if}
      <span class="sr-only min-[1900px]:not-sr-only"
        >{t.attachments}{editor.attachments.length > 0 ? ` (${editor.attachments.length})` : ''}</span
      >
      {#if editor.attachments.length > 0}<span class="min-[1900px]:hidden" aria-hidden="true"
          >{editor.attachments.length}</span
        >{/if}
    </button>
    <button
      type="button"
      class={button}
      disabled={editor.busy}
      title={editor.pdfa ? t.isPdfa(editor.pdfa) : t.toPdfa}
      onclick={() => editor.openPdfa()}
    >
      <Archive class="size-4" />
      <span class="sr-only min-[1900px]:not-sr-only">{editor.pdfa ? `PDF/A-${editor.pdfa}` : 'PDF/A'}</span>
    </button>
    <button
      type="button"
      class={[button, editor.signaturesOpen && pressed]}
      aria-pressed={editor.signaturesOpen}
      title={t.signaturesTitle}
      onclick={() => {
        editor.signaturesOpen = !editor.signaturesOpen;
        if (editor.signaturesOpen) editor.attachmentsOpen = false;
      }}
    >
      <Signature class="size-4" />
      <span class="sr-only min-[1900px]:not-sr-only"
        >{t.signatures}{editor.signatures.length > 0 ? ` (${editor.signatures.length})` : ''}</span
      >
      {#if editor.signatures.length > 0}<span class="min-[1900px]:hidden" aria-hidden="true"
          >{editor.signatures.length}</span
        >{/if}
    </button>
    <span class={separator} aria-hidden="true"></span>
    <button
      type="button"
      class={button}
      disabled={!editor.canUndo || editor.busy}
      onclick={() => editor.undo()}
      title={t.undo}
    >
      <Undo2 class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.undo}</span>
    </button>
    <button
      type="button"
      class={[primaryButton, 'ms-1']}
      disabled={editor.busy}
      onclick={() => editor.save()}
      title={editor.signed ? t.saveSigned : editor.path ? t.overwrites(editor.path) : undefined}
    >
      <FileDown class="size-4" />
      {t.save}
    </button>
    {#if choosesWhere}
      <button type="button" class={button} disabled={editor.busy} onclick={() => editor.save(true)} title={t.saveAs}>
        <FileDown class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.saveAs}</span>
      </button>
    {/if}
  </div>
  <ZoomControl percent={editor.gridZoom} range={gridRange} onzoom={(percent) => editor.zoomGrid(percent)} />
  <div class="flex items-center gap-1">
    <MoreMenu />
    <button
      type="button"
      class={[button, 'text-muted-foreground hover:text-foreground']}
      aria-label={t.close}
      title={t.close}
      onclick={() => {
        if (!editor.modified || confirm(t.closeUnsaved)) editor.close();
      }}
    >
      <X class="size-4" />
    </button>
  </div>
</header>
