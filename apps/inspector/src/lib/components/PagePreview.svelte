<script lang="ts">
  // The page as the engine draws it, two ways from the same interpreted page: vector (SVG, text in a
  // generic font) or raster (the engine's Blend2D renderer, text as glyph boxes). Images the engine
  // cannot decode yet (JPEG and the other codecs) are placeholders in both.
  // The SVG is shown as an image: a browser runs no script and loads nothing from an <img>.
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import type { PagePreview, PageRaster } from '$lib/engine/types';
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
      {#if raster}
        {#await raster}
          <p class="text-sm text-muted-foreground">Rendering della pagina…</p>
        {:then image}
          <div role="img" aria-label="Anteprima raster della pagina {preview.page + 1}">
            <canvas
              {@attach (node) => paint(node, image)}
              class="mx-auto block bg-white shadow-sm"
              style:width
              style:max-width={maxWidth}
            ></canvas>
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
          class="mx-auto block bg-white shadow-sm"
          style:width
          style:max-width={maxWidth}
        />
      {/if}
    </div>
    <p class="mt-2 text-xs text-muted-foreground">
      {mode === 'raster'
        ? `Raster, dal renderer del motore (Blend2D) a ${Math.round(dpi)} dpi: il testo è disegnato come riquadri dei glifi.`
        : "Vettoriale, dall'interprete del motore: il testo è in un carattere generico."}
      Le immagini JPEG, JPEG 2000, JBIG2 e CCITT sono ancora segnaposto.
      {#each Object.entries(kindLabel) as [kind, label] (kind)}
        {#if preview.commands[kind as keyof PagePreview['commands']]}
          · {preview.commands[kind as keyof PagePreview['commands']]} {label}
        {/if}
      {/each}
    </p>
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
