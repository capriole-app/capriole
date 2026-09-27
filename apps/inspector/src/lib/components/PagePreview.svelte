<script lang="ts">
  // A vector preview of the page, drawn by the engine's interpreter: paths, colours and clips as the
  // file says; text in a generic font; images as framed placeholders (the renderer comes later).
  // The SVG is shown as an image: a browser runs no script and loads nothing from an <img>.
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import type { PagePreview } from '$lib/engine/types';
  import { getInspector } from '$lib/inspector.svelte';

  let { index }: { index: number } = $props();

  const inspector = getInspector();
  const request = $derived(inspector.engine.pagePreview(index));

  let zoom = $state<'fit' | 1 | 2>('fit');
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
      <div class="ms-auto flex rounded-md border text-sm" role="group" aria-label="Zoom">
        {#each zooms as [key, label] (key)}
          <button
            type="button"
            class={['px-2 py-1 not-first:border-s', zoom === key && 'bg-accent']}
            onclick={() => (zoom = key)}>{label}</button
          >
        {/each}
      </div>
    </div>
    <div class="max-h-[80vh] overflow-auto rounded-lg border bg-muted p-3">
      <img
        {@attach (node) => svgImage(node, preview)}
        alt="Anteprima della pagina {preview.page + 1}"
        class="mx-auto block bg-white shadow-sm"
        style:width={zoom === 'fit' ? '100%' : `${preview.width * zoom * (96 / 72)}px`}
        style:max-width={zoom === 'fit' ? `${preview.width * 2}px` : 'none'}
      />
    </div>
    <p class="mt-2 text-xs text-muted-foreground">
      Vettoriale, dall'interprete del motore: il testo è in un carattere generico e le immagini sono segnaposto finché
      non arriva il renderer.
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
