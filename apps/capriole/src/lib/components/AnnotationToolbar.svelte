<script lang="ts">
  // The page shown large: back to the pages, the page before and after, the tools that make
  // annotations or edit the page's text, the colour of the next one, undo and save.
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
  import Signature from '@lucide/svelte/icons/signature';
  import Square from '@lucide/svelte/icons/square';
  import StickyNote from '@lucide/svelte/icons/sticky-note';
  import Strikethrough from '@lucide/svelte/icons/strikethrough';
  import TextCursorInput from '@lucide/svelte/icons/text-cursor-input';
  import Type from '@lucide/svelte/icons/type';
  import Underline from '@lucide/svelte/icons/underline';
  import Undo2 from '@lucide/svelte/icons/undo-2';
  import BrandMark from './BrandMark.svelte';
  import LanguageMenu from './LanguageMenu.svelte';
  import ThemeMenu from './ThemeMenu.svelte';
  import ZoomControl from './ZoomControl.svelte';
  import { pageRange } from '$lib/zoom';
  import { getEditor, type Rgb, type Tool } from '$lib/editor.svelte';
  import { colors, css, sameColor } from '$lib/colors';
  import { tr } from '$lib/i18n/index.svelte';
  import { annotationToolbarWords } from '$lib/i18n/annotationToolbar';

  const editor = getEditor();
  const t = $derived(tr(annotationToolbarWords));
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
  ];
  const tools = $derived(hasForm ? all : all.filter((t) => t.tool !== 'form'));
  // The size of the next free text, or of the free text selected.
  const sizes = [8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72];
  const selectedText = $derived(editor.annotation?.subtype === 'FreeText' ? editor.annotation : null);
  const button =
    'flex items-center justify-center rounded-md border hover:bg-muted disabled:pointer-events-none disabled:opacity-50';
</script>

<header class="flex flex-wrap items-center gap-2 border-b bg-card px-4 py-2">
  <BrandMark label={t.about} />
  <button type="button" class={[button, 'gap-1.5 px-2.5 py-1.5 text-sm']} onclick={() => editor.closeView()}>
    <LayoutGrid class="size-4" />
    {t.pages}
  </button>
  <div class="flex items-center gap-1">
    <button
      type="button"
      class={[button, 'size-8']}
      aria-label={t.previous}
      disabled={page === 0 || editor.busy}
      onclick={() => editor.view(page - 1)}><ChevronLeft class="size-4" /></button
    >
    <span class="min-w-24 text-center text-sm tabular-nums" aria-live="polite">{t.pageOf(page + 1, editor.pages)}</span>
    <button
      type="button"
      class={[button, 'size-8']}
      aria-label={t.next}
      disabled={page >= editor.pages - 1 || editor.busy}
      onclick={() => editor.view(page + 1)}><ChevronRight class="size-4" /></button
    >
  </div>

  <ZoomControl
    percent={editor.pageZoomShown}
    range={pageRange}
    onzoom={(percent) => (editor.pageZoom = percent)}
    fitted={editor.pageZoom === 'fit'}
    onfit={() => (editor.pageZoom = 'fit')}
  />

  <div class="flex flex-wrap items-center gap-1" role="toolbar" aria-label={t.tools}>
    {#each tools as { tool, icon: Icon } (tool)}
      <button
        type="button"
        class={[button, 'size-9', editor.tool === tool && 'border-primary bg-accent']}
        aria-label={t[tool]}
        title={t[tool]}
        aria-pressed={editor.tool === tool}
        onclick={() => (editor.tool = tool)}><Icon class="size-4" /></button
      >
    {/each}
  </div>

  <div class="flex items-center gap-1" role="radiogroup" aria-label={t.color}>
    {#each colors as { name, rgb } (name)}
      <button
        type="button"
        role="radio"
        aria-checked={sameColor(editor.color, rgb)}
        aria-label={name}
        title={name}
        class={[
          'size-6 rounded-full border-2',
          sameColor(editor.color, rgb) ? 'border-foreground' : 'border-transparent',
        ]}
        style:background-color={css(rgb)}
        onclick={() => (editor.color = [...rgb] as Rgb)}
      ></button>
    {/each}
  </div>

  <label class="flex items-center gap-1.5 text-sm" title={t.textSize}>
    <Type class="size-4 text-muted-foreground" aria-hidden="true" />
    <select
      class="rounded-md border bg-background px-1.5 py-1"
      aria-label={t.textSize}
      value={selectedText?.fontSize ? Math.round(selectedText.fontSize) : editor.fontSize}
      onchange={(e) => {
        const size = Number(e.currentTarget.value);
        editor.fontSize = size;
        if (selectedText?.ref) editor.changeAnnotation(selectedText.ref, { fontSize: size });
      }}
    >
      {#each sizes as size (size)}
        <option value={size}>{size} pt</option>
      {/each}
    </select>
  </label>

  {#if editor.redactionMarks.length > 0}
    <button
      type="button"
      class="flex items-center gap-1.5 rounded-md bg-danger px-3 py-1.5 text-sm font-medium text-white disabled:opacity-60"
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

  <div class="ms-auto flex items-center gap-2">
    <button
      type="button"
      class={[button, 'gap-1.5 px-2.5 py-1.5 text-sm']}
      disabled={!editor.canUndo || editor.busy}
      onclick={() => editor.undo()}><Undo2 class="size-4" /> {t.undo}</button
    >
    <button
      type="button"
      class="flex items-center gap-1.5 rounded-md bg-primary px-3 py-1.5 text-sm font-medium text-primary-foreground disabled:opacity-60"
      disabled={editor.busy}
      onclick={() => editor.save()}><FileDown class="size-4" /> {t.save}</button
    >
    <LanguageMenu />
    <ThemeMenu />
  </div>
</header>
