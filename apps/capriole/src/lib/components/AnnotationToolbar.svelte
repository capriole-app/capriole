<script lang="ts">
  // The page shown large: back to the pages, the page before and after, its zoom and rotation, the tools
  // that make annotations or edit the page's text, the colour of the next one, undo and save. On one line
  // from 1440 px: below 1600 px «Pagine», «Adatta» and «Annulla» keep only their icon (and their name for
  // screen readers). On a phone: back to the pages, the page before and after, undo and save on top; the
  // tools and the colour along the bottom; the rest (open, zoom, rotation, the page's details) in ⋯.
  import ChevronLeft from '@lucide/svelte/icons/chevron-left';
  import ChevronRight from '@lucide/svelte/icons/chevron-right';
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import Maximize from '@lucide/svelte/icons/maximize';
  import PanelBottom from '@lucide/svelte/icons/panel-bottom';
  import ZoomIn from '@lucide/svelte/icons/zoom-in';
  import ZoomOut from '@lucide/svelte/icons/zoom-out';
  import Circle from '@lucide/svelte/icons/circle';
  import Eraser from '@lucide/svelte/icons/eraser';
  import FileDown from '@lucide/svelte/icons/file-down';
  import FormInput from '@lucide/svelte/icons/form-input';
  import Highlighter from '@lucide/svelte/icons/highlighter';
  import LayoutGrid from '@lucide/svelte/icons/layout-grid';
  import Minus from '@lucide/svelte/icons/minus';
  import MousePointer2 from '@lucide/svelte/icons/mouse-pointer-2';
  import PenLine from '@lucide/svelte/icons/pen-line';
  import RotateCcw from '@lucide/svelte/icons/rotate-ccw';
  import RotateCw from '@lucide/svelte/icons/rotate-cw';
  import Signature from '@lucide/svelte/icons/signature';
  import Square from '@lucide/svelte/icons/square';
  import Stamp from '@lucide/svelte/icons/stamp';
  import StickyNote from '@lucide/svelte/icons/sticky-note';
  import Strikethrough from '@lucide/svelte/icons/strikethrough';
  import TextCursorInput from '@lucide/svelte/icons/text-cursor-input';
  import Type from '@lucide/svelte/icons/type';
  import Underline from '@lucide/svelte/icons/underline';
  import Undo2 from '@lucide/svelte/icons/undo-2';
  import BrandMark from './BrandMark.svelte';
  import MoreMenu from './MoreMenu.svelte';
  import OpenButton from './OpenButton.svelte';
  import StyleMenu from './StyleMenu.svelte';
  import ZoomControl from './ZoomControl.svelte';
  import { pageRange, zoomIn, zoomOut } from '$lib/zoom';
  import { graphicSignatures, showPicture } from '$lib/graphic-signatures.svelte';
  import { graphicSignatureWords } from '$lib/i18n/graphicSignatures';
  import { getEditor, type Tool } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { annotationToolbarWords } from '$lib/i18n/annotationToolbar';
  import { toolbarWords } from '$lib/i18n/toolbar';
  import { zoomWords } from '$lib/i18n/zoom';
  import {
    barButton as button,
    bottomBar,
    bottomScroll,
    menuItem,
    pressed,
    primaryButton,
    separator,
    toolButton,
  } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(annotationToolbarWords));
  const g = $derived(tr(graphicSignatureWords));
  const tp = $derived(tr(toolbarWords));
  const tz = $derived(tr(zoomWords));
  let opener: OpenButton | undefined = $state();
  const page = $derived(editor.viewing ?? 0);

  // Filling the form comes first when the document has one.
  const hasForm = $derived(editor.fields.some((f) => f.fillable));
  const all: { tool: Extract<Tool, keyof typeof t>; icon: typeof Highlighter }[] = [
    { tool: 'form', icon: FormInput },
    { tool: 'select', icon: MousePointer2 },
    { tool: 'edittext', icon: TextCursorInput },
    { tool: 'highlight', icon: Highlighter },
    { tool: 'underline', icon: Underline },
    { tool: 'strikeout', icon: Strikethrough },
    { tool: 'note', icon: StickyNote },
    { tool: 'ink', icon: PenLine },
    { tool: 'square', icon: Square },
    { tool: 'circle', icon: Circle },
    { tool: 'line', icon: Minus },
    { tool: 'freetext', icon: Type },
    { tool: 'redact', icon: Eraser },
    { tool: 'sign', icon: Signature },
    { tool: 'stamp', icon: Stamp },
  ];
  const tools = $derived(hasForm ? all : all.filter((t) => t.tool !== 'form'));
  /** A button's text that only the wide screens show. */
  const wide = 'sr-only min-[1600px]:not-sr-only';
</script>

<header
  class="flex flex-wrap items-center gap-x-1.5 gap-y-2 border-b bg-background px-3 py-2 max-md:gap-x-0.5 max-md:px-2"
>
  <div class="max-md:hidden"><BrandMark label={t.about} /></div>
  <OpenButton bind:this={opener} text="sr-only min-[1900px]:not-sr-only" class="max-md:hidden" />
  <button type="button" class={button} title={t.pages} onclick={() => editor.closeView()}>
    <LayoutGrid class="size-4" />
    <span class={wide}>{t.pages}</span>
  </button>
  <div class="flex items-center gap-1">
    <button
      type="button"
      class={button}
      aria-label={t.previous}
      disabled={page === 0 || editor.busy}
      onclick={() => editor.view(page - 1)}><ChevronLeft class="size-4" /></button
    >
    <span class="text-center text-sm tabular-nums md:min-w-24" aria-live="polite"
      ><span class="max-md:hidden">{t.pageOf(page + 1, editor.pages)}</span><span class="md:hidden"
        >{page + 1}/{editor.pages}</span
      ></span
    >
    <button
      type="button"
      class={button}
      aria-label={t.next}
      disabled={page >= editor.pages - 1 || editor.busy}
      onclick={() => editor.view(page + 1)}><ChevronRight class="size-4" /></button
    >
  </div>

  <span class={[separator, 'max-md:hidden']} aria-hidden="true"></span>
  <div class="max-md:hidden">
    <ZoomControl
      percent={editor.pageZoomShown}
      range={pageRange}
      onzoom={(percent) => (editor.pageZoom = percent)}
      fitted={editor.pageZoom === 'fit'}
      onfit={() => (editor.pageZoom = 'fit')}
    />
  </div>
  <div class="flex items-center gap-1 max-md:hidden">
    <button
      type="button"
      class={button}
      aria-label={tp.rotateLeft}
      title={tp.rotateLeft}
      disabled={editor.busy}
      onclick={() => editor.rotateShown(-90)}><RotateCcw class="size-4" /></button
    >
    <button
      type="button"
      class={button}
      aria-label={tp.rotateRight}
      title={tp.rotateRight}
      disabled={editor.busy}
      onclick={() => editor.rotateShown(90)}><RotateCw class="size-4" /></button
    >
  </div>

  <div class={bottomBar} data-testid="bottom-bar">
    <span class={[separator, 'max-md:hidden']} aria-hidden="true"></span>
    <div class={['flex flex-wrap items-center gap-0.5', bottomScroll]} role="toolbar" aria-label={t.tools}>
      {#each tools as { tool, icon: Icon } (tool)}
        <button
          type="button"
          class={[toolButton, editor.tool === tool && pressed]}
          aria-label={t[tool]}
          title={t[tool]}
          aria-pressed={editor.tool === tool}
          data-tool={tool}
          onclick={() => (editor.tool = tool)}><Icon class="size-4" /></button
        >
      {/each}
    </div>

    {#if editor.tool === 'stamp'}
      <!-- The graphic signature to put: one of those kept (settings), or a way to add one. -->
      <div class="flex items-center gap-1" role="radiogroup" aria-label={g.choose} data-testid="stamp-choice">
        {#each graphicSignatures.list as s (s.id)}
          <button
            type="button"
            role="radio"
            aria-checked={graphicSignatures.chosen?.id === s.id}
            aria-label={s.name}
            title={s.name}
            class={[
              'flex h-8 w-16 items-center justify-center rounded-md border-2 bg-white px-1',
              graphicSignatures.chosen?.id === s.id ? 'border-emphasis' : 'border-transparent',
            ]}
            onclick={() => graphicSignatures.choose(s.id)}
          >
            <img {@attach showPicture(s.png)} alt="" class="max-h-6 max-w-full" />
          </button>
        {/each}
        <button type="button" class={button} onclick={() => (editor.settingsOpen = 'signatures')}
          >{graphicSignatures.list.length === 0 ? g.add : g.manage}</button
        >
      </div>
    {/if}

    <span class={[separator, 'max-md:hidden']} aria-hidden="true"></span>
    <!-- Last on a phone, outside what scrolls: its menu opens above it. -->
    <div class="max-md:order-last"><StyleMenu /></div>

    {#if editor.redactionMarks.length > 0}
      <button
        type="button"
        class="flex h-9 items-center gap-1.5 rounded-md bg-danger px-3 text-sm font-medium text-white disabled:opacity-60"
        disabled={editor.busy}
        onclick={() => {
          const n = editor.redactionMarks.length;
          if (confirm(t.redactConfirm(n, editor.signed))) editor.applyRedactions();
        }}
      >
        <Eraser class="size-4" />
        <span class="max-md:sr-only">{t.applyRedaction(editor.redactionMarks.length)}</span>
        <span class="md:hidden" aria-hidden="true">{editor.redactionMarks.length}</span>
      </button>
    {/if}
  </div>

  <div class="ms-auto flex items-center gap-1">
    <button
      type="button"
      class={button}
      title={t.undo}
      disabled={!editor.canUndo || editor.busy}
      onclick={() => editor.undo()}><Undo2 class="size-4" /> <span class={wide}>{t.undo}</span></button
    >
    <button
      type="button"
      class={[primaryButton, 'max-md:px-3 md:ms-1']}
      disabled={editor.busy}
      onclick={() => editor.save()}><FileDown class="size-4" /> {t.save}</button
    >
    <span class={[separator, 'max-md:hidden']} aria-hidden="true"></span>
    <MoreMenu>
      {#snippet phone()}
        <button type="button" role="menuitem" class={menuItem} disabled={editor.busy} onclick={() => opener?.choose()}>
          <FolderOpen class="size-4 text-muted-foreground" />
          {tp.open}
        </button>
        <button
          type="button"
          role="menuitem"
          class={menuItem}
          onclick={() => (editor.pageZoom = zoomIn(editor.pageZoomShown, pageRange))}
        >
          <ZoomIn class="size-4 text-muted-foreground" />
          {tz.zoomIn}
        </button>
        <button
          type="button"
          role="menuitem"
          class={menuItem}
          onclick={() => (editor.pageZoom = zoomOut(editor.pageZoomShown, pageRange))}
        >
          <ZoomOut class="size-4 text-muted-foreground" />
          {tz.zoomOut}
        </button>
        <button type="button" role="menuitem" class={menuItem} onclick={() => (editor.pageZoom = 'fit')}>
          <Maximize class="size-4 text-muted-foreground" />
          {tz.fitTitle}
        </button>
        <button
          type="button"
          role="menuitem"
          class={menuItem}
          disabled={editor.busy}
          onclick={() => editor.rotateShown(-90)}
        >
          <RotateCcw class="size-4 text-muted-foreground" />
          {tp.rotateLeft}
        </button>
        <button
          type="button"
          role="menuitem"
          class={menuItem}
          disabled={editor.busy}
          onclick={() => editor.rotateShown(90)}
        >
          <RotateCw class="size-4 text-muted-foreground" />
          {tp.rotateRight}
        </button>
        <button
          type="button"
          role="menuitem"
          class={menuItem}
          title={t.detailsTitle}
          data-testid="open-details"
          onclick={() => (editor.detailsOpen = true)}
        >
          <PanelBottom class="size-4 text-muted-foreground" />
          {t.details}
        </button>
      {/snippet}
    </MoreMenu>
  </div>
</header>
