<script lang="ts">
  // The page as the engine draws it, two ways from the same interpreted page: vector (SVG, glyphs as
  // outlines) or raster (the engine's Blend2D renderer).
  // The SVG is shown as an image: a browser runs no script and loads nothing from an <img>.
  // Over either, the page's text (TextLayer): selectable in reading order, each word traced back to the
  // operator it comes from (TextPanel).
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import TextLayer from './TextLayer.svelte';
  import TextPanel from './TextPanel.svelte';
  import type { PagePreview, PageRaster, TextOrder } from '@capriole/engine-client';
  import { getInspector } from '$lib/inspector.svelte';

  let { index }: { index: number } = $props();

  const inspector = getInspector();
  const request = $derived(inspector.engine.pagePreview(index));

  let zoom = $state<'fit' | 1 | 2>('fit');
  let mode = $state<'vector' | 'raster'>('vector');
  const modes = [
    ['vector', 'vettoriale'],
    ['raster', 'raster'],
  ] as const;

  // Enough pixels for the size on screen (fit shows up to twice the page's size in points), sharp on
  // high-density screens, and bounded: the engine refuses bitmaps beyond 50 million pixels.
  const dpi = $derived(Math.min(96 * (zoom === 'fit' ? 2 : zoom) * (globalThis.devicePixelRatio || 1), 400));
  const raster = $derived(mode === 'raster' ? inspector.engine.pageRender(index, dpi) : null);

  let order = $state<TextOrder>('content');
  let outlined = $state(false);
  let selected = $state<number | null>(null);
  const text = $derived(inspector.engine.pageText(index, order));
  const orders = [
    ['content', 'ordine del contenuto'],
    ['layout', 'per colonne'],
  ] as const;

  function setOrder(next: TextOrder) {
    order = next;
    selected = null;
  }

  /** A word picked in the text layer: described in the panel, its operator shown in the page's content. */
  function select(word: number, words: number, offsetOf: (word: number) => number | null) {
    if (word < 0 || word >= words) return;
    selected = word;
    const offset = offsetOf(word);
    inspector.highlighted = offset === null ? null : { page: index, offset };
  }

  /** Paints the engine's pixels into the canvas. */
  function paint(canvas: HTMLCanvasElement, image: PageRaster) {
    canvas.width = image.width;
    canvas.height = image.height;
    const pixels = new ImageData(new Uint8ClampedArray(image.pixels), image.width, image.height);
    canvas.getContext('2d')?.putImageData(pixels, 0, 0);
  }

  const zooms = [
    ['fit', 'adatta'],
    [1, '100%'],
    [2, '200%'],
  ] as const;

  /** A blob URL for the SVG, released when the preview changes or the component goes away. */
  function svgImage(node: HTMLImageElement, preview: PagePreview) {
    const url = URL.createObjectURL(new Blob([preview.svg], { type: 'image/svg+xml' }));
    node.src = url;
    return () => URL.revokeObjectURL(url);
  }

  const kindLabel: Record<string, string> = {
    fill: 'riempimenti',
    stroke: 'tratti',
    clip: 'ritagli',
    text: 'testi',
    image: 'immagini',
    shading: 'sfumature',
    pattern: 'pattern a tessere',
    group: 'gruppi di trasparenza',
    soft_mask: 'soft mask',
  };
</script>

<section class="mt-6">
  {#await request}
    <p class="text-sm text-muted-foreground">Interpretazione della pagina…</p>
  {:then preview}
    <div class="mb-2 flex flex-wrap items-center gap-3">
      <h3 class="font-semibold">Anteprima della pagina {preview.page + 1}</h3>
      <span class="text-sm text-muted-foreground">
        {Math.round(preview.width)} × {Math.round(preview.height)} pt{preview.rotate
          ? ` · ruotata ${preview.rotate}°`
          : ''}
      </span>
      <div class="ms-auto flex rounded-md border text-sm" role="group" aria-label="Resa">
        {#each modes as [key, label] (key)}
          <button
            type="button"
            class={['px-2 py-1 not-first:border-s', mode === key && 'bg-accent']}
            aria-pressed={mode === key}
            onclick={() => (mode = key)}>{label}</button
          >
        {/each}
      </div>
      <div class="flex rounded-md border text-sm" role="group" aria-label="Zoom">
        {#each zooms as [key, label] (key)}
          <button
            type="button"
            class={['px-2 py-1 not-first:border-s', zoom === key && 'bg-accent']}
            onclick={() => (zoom = key)}>{label}</button
          >
        {/each}
      </div>
    </div>
    {@const width = zoom === 'fit' ? '100%' : `${preview.width * zoom * (96 / 72)}px`}
    {@const maxWidth = zoom === 'fit' ? `${preview.width * 2}px` : 'none'}
    <div class="max-h-[80vh] overflow-auto rounded-lg border bg-muted p-3">
      <div class="relative mx-auto" style:width style:max-width={maxWidth}>
        {#if raster}
          {#await raster}
            <p class="text-sm text-muted-foreground">Rendering della pagina…</p>
          {:then image}
            <div role="img" aria-label="Anteprima raster della pagina {preview.page + 1}">
              <canvas {@attach (node) => paint(node, image)} class="block w-full bg-white shadow-sm"></canvas>
            </div>
          {:catch error}
            <p class="rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">
              {error instanceof Error ? error.message : String(error)}
            </p>
          {/await}
        {:else}
          <img
            {@attach (node) => svgImage(node, preview)}
            alt="Anteprima della pagina {preview.page + 1}"
            class="block w-full bg-white shadow-sm"
          />
        {/if}
        {#await text then pageText}
          <TextLayer
            text={pageText}
            {selected}
            {outlined}
            onselect={(word) =>
              select(word, pageText.words.length, (w) => {
                const char = pageText.chars[pageText.words[w]![4]];
                return char && char[8] === null ? char[9] : null;
              })}
          />
        {/await}
      </div>
    </div>
    <p class="mt-2 text-xs text-muted-foreground">
      {mode === 'raster'
        ? `Raster, dal renderer del motore (Blend2D) a ${Math.round(dpi)} dpi.`
        : "Vettoriale, dall'interprete del motore (SVG, i glifi come tracciati)."}
      Sopra, il testo estratto: selezionabile, trasparente.
      {#each Object.entries(kindLabel) as [kind, label] (kind)}
        {#if preview.commands[kind as keyof PagePreview['commands']]}
          · {preview.commands[kind as keyof PagePreview['commands']]} {label}
        {/if}
      {/each}
    </p>
    <div class="mt-3 flex flex-wrap items-center gap-3">
      <h4 class="font-semibold">Testo</h4>
      <div class="flex rounded-md border text-sm" role="group" aria-label="Ordine di lettura">
        {#each orders as [key, label] (key)}
          <button
            type="button"
            class={['px-2 py-1 not-first:border-s', order === key && 'bg-accent']}
            aria-pressed={order === key}
            onclick={() => setOrder(key)}>{label}</button
          >
        {/each}
      </div>
      <label class="flex items-center gap-2 text-sm">
        <input type="checkbox" bind:checked={outlined} /> riquadri delle parole
      </label>
    </div>
    {#await text}
      <p class="mt-2 text-sm text-muted-foreground">Estrazione del testo…</p>
    {:then pageText}
      <TextPanel text={pageText} {selected} />
    {:catch error}
      <p class="mt-2 rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">
        {error instanceof Error ? error.message : String(error)}
      </p>
    {/await}
    {#if preview.warnings.length > 0}
      <details class="mt-2 rounded-md bg-warning-background px-3 py-2 text-sm text-warning">
        <summary class="flex cursor-pointer items-center gap-2">
          <TriangleAlert class="size-4" /> Approssimazioni e avvisi: {preview.warnings.length}
        </summary>
        <ul class="mt-2 list-disc ps-6 text-xs">
          {#each preview.warnings as warning, i (i)}
            <li>{warning}</li>
          {/each}
        </ul>
      </details>
    {/if}
  {:catch error}
    <p class="rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">
      {error instanceof Error ? error.message : String(error)}
    </p>
  {/await}
</section>
