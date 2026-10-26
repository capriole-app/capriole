<script lang="ts">
  // The open file and what can be done to its pages. Actions on pages apply to the selection. On a phone the
  // actions on pages are along the bottom of the screen, and what is about the whole document is in ⋯.
  import Archive from '@lucide/svelte/icons/archive';
  import ArrowLeft from '@lucide/svelte/icons/arrow-left';
  import ArrowRight from '@lucide/svelte/icons/arrow-right';
  import Copy from '@lucide/svelte/icons/copy';
  import FileDown from '@lucide/svelte/icons/file-down';
  import FileOutput from '@lucide/svelte/icons/file-output';
  import FilePlus from '@lucide/svelte/icons/file-plus';
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import Highlighter from '@lucide/svelte/icons/highlighter';
  import Paperclip from '@lucide/svelte/icons/paperclip';
  import ReceiptText from '@lucide/svelte/icons/receipt-text';
  import RotateCcw from '@lucide/svelte/icons/rotate-ccw';
  import RotateCw from '@lucide/svelte/icons/rotate-cw';
  import Signature from '@lucide/svelte/icons/signature';
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import Undo2 from '@lucide/svelte/icons/undo-2';
  import X from '@lucide/svelte/icons/x';
  import ZoomIn from '@lucide/svelte/icons/zoom-in';
  import ZoomOut from '@lucide/svelte/icons/zoom-out';
  import BrandMark from './BrandMark.svelte';
  import MoreMenu from './MoreMenu.svelte';
  import OpenButton from './OpenButton.svelte';
  import ZoomControl from './ZoomControl.svelte';
  import { gridRange, zoomIn, zoomOut } from '$lib/zoom';
  import { getEditor } from '$lib/editor.svelte';
  import { choosesWhere, fromBrowser } from '$lib/platform';
  import { tr } from '$lib/i18n/index.svelte';
  import { toolbarWords } from '$lib/i18n/toolbar';
  import { zoomWords } from '$lib/i18n/zoom';
  import { barButton as button, bottomBar, menuItem, pressed, primaryButton, separator } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(toolbarWords));
  const tz = $derived(tr(zoomWords));
  let input: HTMLInputElement | undefined = $state();
  let opener: OpenButton | undefined = $state();

  const some = $derived(editor.selected.size > 0);
  const all = $derived(editor.selected.size === editor.pages);

  const insert = () => (choosesWhere ? editor.insertFromDialog() : input?.click());
  function toggleAttachments() {
    editor.attachmentsOpen = !editor.attachmentsOpen;
    if (editor.attachmentsOpen) editor.signaturesOpen = false;
  }
  function toggleSignatures() {
    editor.signaturesOpen = !editor.signaturesOpen;
    if (editor.signaturesOpen) editor.attachmentsOpen = false;
  }
  function close() {
    if (!editor.modified || confirm(t.closeUnsaved)) editor.close();
  }
</script>

<header class="flex flex-wrap items-center gap-x-3 gap-y-2 border-b bg-background px-4 py-2">
  <BrandMark label={t.about} />
  <OpenButton bind:this={opener} text="sr-only min-[1900px]:not-sr-only" class="max-md:hidden" />
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
    <div class={[bottomBar, 'max-md:overflow-x-auto']} data-testid="bottom-bar">
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
      <button type="button" class={[button, 'max-md:hidden']} disabled={editor.busy} onclick={insert} title={t.insert}>
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
      <span class={[separator, 'max-md:hidden']} aria-hidden="true"></span>
      <button
        type="button"
        class={[button, 'max-md:hidden', editor.attachmentsOpen && pressed]}
        aria-pressed={editor.attachmentsOpen}
        title={editor.facturx ? t.invoiceTitle : t.attachmentsTitle}
        onclick={toggleAttachments}
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
        class={[button, 'max-md:hidden']}
        disabled={editor.busy}
        title={editor.pdfa ? t.isPdfa(editor.pdfa) : t.toPdfa}
        onclick={() => editor.openPdfa()}
      >
        <Archive class="size-4" />
        <span class="sr-only min-[1900px]:not-sr-only">{editor.pdfa ? `PDF/A-${editor.pdfa}` : 'PDF/A'}</span>
      </button>
      <button
        type="button"
        class={[button, 'max-md:hidden', editor.signaturesOpen && pressed]}
        aria-pressed={editor.signaturesOpen}
        title={t.signaturesTitle}
        onclick={toggleSignatures}
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
    </div>
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
      <button
        type="button"
        class={[button, 'max-md:hidden']}
        disabled={editor.busy}
        onclick={() => editor.save(true)}
        title={t.saveAs}
      >
        <FileDown class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">{t.saveAs}</span>
      </button>
    {/if}
  </div>
  <div class="max-md:hidden">
    <ZoomControl percent={editor.gridZoom} range={gridRange} onzoom={(percent) => editor.zoomGrid(percent)} />
  </div>
  <div class="flex items-center gap-1">
    <MoreMenu>
      {#snippet phone()}
        <button type="button" role="menuitem" class={menuItem} disabled={editor.busy} onclick={() => opener?.choose()}>
          <FolderOpen class="size-4 text-muted-foreground" />
          {t.open}
        </button>
        <button type="button" role="menuitem" class={menuItem} disabled={editor.busy} onclick={insert}>
          <FilePlus class="size-4 text-muted-foreground" />
          {t.insert}
        </button>
        {#if choosesWhere}
          <button
            type="button"
            role="menuitem"
            class={menuItem}
            disabled={editor.busy}
            onclick={() => editor.save(true)}
          >
            <FileDown class="size-4 text-muted-foreground" />
            {t.saveAs}
          </button>
        {/if}
        <button type="button" role="menuitem" class={menuItem} onclick={toggleAttachments}>
          <Paperclip class="size-4 text-muted-foreground" />
          {t.attachments}{editor.attachments.length > 0 ? ` (${editor.attachments.length})` : ''}
        </button>
        <button type="button" role="menuitem" class={menuItem} onclick={toggleSignatures}>
          <Signature class="size-4 text-muted-foreground" />
          {t.signatures}{editor.signatures.length > 0 ? ` (${editor.signatures.length})` : ''}
        </button>
        <button type="button" role="menuitem" class={menuItem} disabled={editor.busy} onclick={() => editor.openPdfa()}>
          <Archive class="size-4 text-muted-foreground" />
          {editor.pdfa ? `PDF/A-${editor.pdfa}` : 'PDF/A'}
        </button>
        <button
          type="button"
          role="menuitem"
          class={menuItem}
          onclick={() => editor.zoomGrid(zoomIn(editor.gridZoom, gridRange))}
        >
          <ZoomIn class="size-4 text-muted-foreground" />
          {tz.zoomIn}
        </button>
        <button
          type="button"
          role="menuitem"
          class={menuItem}
          onclick={() => editor.zoomGrid(zoomOut(editor.gridZoom, gridRange))}
        >
          <ZoomOut class="size-4 text-muted-foreground" />
          {tz.zoomOut}
        </button>
        <button type="button" role="menuitem" class={menuItem} onclick={close}>
          <X class="size-4 text-muted-foreground" />
          {t.close}
        </button>
      {/snippet}
    </MoreMenu>
    <button
      type="button"
      class={[button, 'text-muted-foreground hover:text-foreground max-md:hidden']}
      aria-label={t.close}
      title={t.close}
      onclick={close}
    >
      <X class="size-4" />
    </button>
  </div>
</header>
