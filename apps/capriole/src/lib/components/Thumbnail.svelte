<script lang="ts">
  // One page, drawn small by the engine's rasterizer once it comes into view (and again after each
  // edit). A button: a click selects it (Ctrl/Cmd adds, Shift extends); it can be dragged to reorder.
  import type { PageRaster } from '@capriole/engine-client';
  import { getEditor } from '$lib/editor.svelte';

  let {
    index,
    drop,
    ondragstart,
    ondragend,
  }: {
    index: number;
    /** Where the dragged pages would land: before or after this page, or not here. */
    drop: 'before' | 'after' | null;
    ondragstart: (event: DragEvent) => void;
    ondragend: () => void;
  } = $props();

  const editor = getEditor();
  let visible = $state(false);

  // Small, sharp on high-density screens: an A4 page is about 160 px wide on screen.
  const dpi = Math.round(20 * Math.min(globalThis.devicePixelRatio || 1, 2));
  // Asked again after each edit (the page at this place may be another); the editor answers from its
  // cache when that page object was drawn already.
  const raster = $derived.by(() => {
    void editor.version;
    return visible ? editor.thumbnail(index, dpi) : null;
  });

  /** Marks the page visible the first time it scrolls into view. */
  function watch(node: HTMLElement) {
    const observer = new IntersectionObserver(
      (entries) => {
        if (entries.some((entry) => entry.isIntersecting)) {
          visible = true;
          observer.disconnect();
        }
      },
      { rootMargin: '200px' },
    );
    observer.observe(node);
    return () => observer.disconnect();
  }

  function paint(canvas: HTMLCanvasElement, image: PageRaster) {
    canvas.width = image.width;
    canvas.height = image.height;
    canvas
      .getContext('2d')
      ?.putImageData(new ImageData(new Uint8ClampedArray(image.pixels), image.width, image.height), 0, 0);
  }

  const selected = $derived(editor.selected.has(index));
</script>

<button
  type="button"
  draggable="true"
  {@attach watch}
  class={[
    'group relative flex flex-col items-center gap-2 rounded-lg p-2 outline-offset-2 hover:bg-accent',
    selected && 'bg-accent ring-2 ring-primary',
  ]}
  aria-pressed={selected}
  aria-label="Pagina {index + 1}"
  data-testid="page"
  onclick={(e) => editor.select(index, { range: e.shiftKey, toggle: e.ctrlKey || e.metaKey })}
  ondblclick={() => editor.view(index)}
  title="Doppio clic per annotare"
  {ondragstart}
  {ondragend}
>
  {#if drop}
    <span
      class={['absolute inset-y-2 w-1 rounded bg-primary', drop === 'before' ? '-start-1.5' : '-end-1.5']}
      aria-hidden="true"
    ></span>
  {/if}
  <span class="flex h-48 w-36 items-center justify-center">
    {#if raster && !(raster instanceof Promise)}
      <canvas
        {@attach (node) => paint(node, raster)}
        class="max-h-48 max-w-36 bg-white shadow-sm ring-1 ring-border"
        style:aspect-ratio="{raster.width} / {raster.height}"
      ></canvas>
    {:else if raster}
      {#await raster}
        <span class="h-44 w-32 animate-pulse rounded bg-muted"></span>
      {:then image}
        <canvas
          {@attach (node) => paint(node, image)}
          class="max-h-48 max-w-36 bg-white shadow-sm ring-1 ring-border"
          style:aspect-ratio="{image.width} / {image.height}"
        ></canvas>
      {:catch}
        <span class="flex h-44 w-32 items-center justify-center rounded bg-danger-background text-xs text-danger"
          >non disegnata</span
        >
      {/await}
    {:else}
      <span class="h-44 w-32 rounded bg-muted"></span>
    {/if}
  </span>
  <span class="text-xs text-muted-foreground tabular-nums">{index + 1}</span>
</button>
