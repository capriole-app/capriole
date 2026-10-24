<script lang="ts">
  // The page shown large: back to the pages, the page before and after, its zoom and rotation, the tools
  // that make annotations or edit the page's text, the colour of the next one, undo and save. On one line
  // from 1440 px: below 1600 px «Pagine», «Adatta» and «Annulla» keep only their icon (and their name for
  // screen readers).
  import ChevronLeft from '@lucide/svelte/icons/chevron-left';
  import ChevronRight from '@lucide/svelte/icons/chevron-right';
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
  import { pageRange } from '$lib/zoom';
  import { graphicSignatures, showPicture } from '$lib/graphic-signatures.svelte';
  import { graphicSignatureWords } from '$lib/i18n/graphicSignatures';
  import { getEditor, type Tool } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { annotationToolbarWords } from '$lib/i18n/annotationToolbar';
  import { toolbarWords } from '$lib/i18n/toolbar';
  import { barButton as button, pressed, primaryButton, separator, toolButton } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(annotationToolbarWords));
  const g = $derived(tr(graphicSignatureWords));
  const tp = $derived(tr(toolbarWords));
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

<header class="flex flex-wrap items-center gap-x-1.5 gap-y-2 border-b bg-background px-3 py-2">
  <BrandMark label={t.about} />
  <OpenButton text="sr-only min-[1900px]:not-sr-only" />
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
    <span class="min-w-24 text-center text-sm tabular-nums" aria-live="polite">{t.pageOf(page + 1, editor.pages)}</span>
    <button
      type="button"
      class={button}
      aria-label={t.next}
      disabled={page >= editor.pages - 1 || editor.busy}
      onclick={() => editor.view(page + 1)}><ChevronRight class="size-4" /></button
    >
  </div>

  <span class={separator} aria-hidden="true"></span>
  <ZoomControl
    percent={editor.pageZoomShown}
    range={pageRange}
    onzoom={(percent) => (editor.pageZoom = percent)}
    fitted={editor.pageZoom === 'fit'}
    onfit={() => (editor.pageZoom = 'fit')}
  />
  <div class="flex items-center gap-1">
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

  <span class={separator} aria-hidden="true"></span>
  <div class="flex flex-wrap items-center gap-0.5" role="toolbar" aria-label={t.tools}>
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

  <span class={separator} aria-hidden="true"></span>
  <StyleMenu />

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
      {t.applyRedaction(editor.redactionMarks.length)}
    </button>
  {/if}

  <div class="ms-auto flex items-center gap-1">
    <button
      type="button"
      class={button}
      title={t.undo}
      disabled={!editor.canUndo || editor.busy}
      onclick={() => editor.undo()}><Undo2 class="size-4" /> <span class={wide}>{t.undo}</span></button
    >
    <button type="button" class={[primaryButton, 'ms-1']} disabled={editor.busy} onclick={() => editor.save()}
      ><FileDown class="size-4" /> {t.save}</button
    >
    <span class={separator} aria-hidden="true"></span>
    <MoreMenu />
  </div>
</header>
