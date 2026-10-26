<script lang="ts">
  // The document's pages as a grid of thumbnails. Dragging moves the selected pages (the dragged page
  // joins the selection first) to where the bar shows: the grid works the position out from the pointer
  // and the nearest page, so gaps and margins take a drop too. The toolbar's arrows do the same from the
  // keyboard.
  import Thumbnail from './Thumbnail.svelte';
  import { getEditor } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { pagesWords } from '$lib/i18n/pages';
  import { gridRange, zoomIn, zoomOut } from '$lib/zoom';

  const editor = getEditor();
  const t = $derived(tr(pagesWords));
  let grid = $state<HTMLDivElement>();
  /** The position the dragged pages would move to (before that page; = pages: at the end). */
  let target = $state<number | null>(null);
  let dragging = false;

  /** The position for a pointer: before the nearest page on its row, or after it past its middle. */
  function positionAt(x: number, y: number): number {
    const pages = [...(grid?.querySelectorAll<HTMLElement>('[data-testid="page"]') ?? [])];
    let best = { distance: Infinity, position: editor.pages };
    pages.forEach((page, index) => {
      const box = page.getBoundingClientRect();
      const dx = x < box.left ? box.left - x : x > box.right ? x - box.right : 0;
      const dy = y < box.top ? box.top - y : y > box.bottom ? y - box.bottom : 0;
      const distance = dy * 10 + dx; // the row first
      if (distance < best.distance) {
        best = { distance, position: x > box.left + box.width / 2 ? index + 1 : index };
      }
    });
    return best.position;
  }

  /** Ctrl/Cmd + wheel zooms the thumbnails (not the browser's zoom). */
  function wheelZoom(node: HTMLElement) {
    const wheel = (e: WheelEvent) => {
      if (!(e.ctrlKey || e.metaKey) || e.deltaY === 0) return;
      e.preventDefault();
      editor.zoomGrid(e.deltaY < 0 ? zoomIn(editor.gridZoom, gridRange) : zoomOut(editor.gridZoom, gridRange));
    };
    node.addEventListener('wheel', wheel, { passive: false });
    return () => node.removeEventListener('wheel', wheel);
  }

  function dropOf(index: number): 'before' | 'after' | null {
    if (target === null) return null;
    if (target === index) return 'before';
    return target === index + 1 && index === editor.pages - 1 ? 'after' : null;
  }
</script>

<!-- On a phone, less margin: two columns at 360 px. -->
<div
  bind:this={grid}
  class="grid grid-cols-[repeat(auto-fill,minmax(var(--column),1fr))] gap-x-4 gap-y-6 p-8 max-md:gap-x-3 max-md:p-3"
  style:--column="{(10 * editor.gridZoom) / 100}rem"
  {@attach wheelZoom}
  role="list"
  aria-label={t.grid}
  ondragover={(e) => {
    if (!dragging) return; // files dragged in from outside are not pages
    e.preventDefault();
    target = positionAt(e.clientX, e.clientY);
  }}
  ondragleave={(e) => {
    if (!e.currentTarget.contains(e.relatedTarget as Node | null)) target = null;
  }}
  ondrop={(e) => {
    if (!dragging) return;
    e.preventDefault();
    const at = target;
    target = null;
    dragging = false;
    if (at !== null) editor.move(at);
  }}
>
  {#each { length: editor.pages } as _, index (index)}
    <div role="listitem">
      <Thumbnail
        {index}
        drop={dropOf(index)}
        ondragstart={(e) => {
          if (!editor.selected.has(index)) editor.select(index);
          dragging = true;
          e.dataTransfer?.setData('text/plain', 'capriole-pages');
          if (e.dataTransfer) e.dataTransfer.effectAllowed = 'move';
        }}
        ondragend={() => {
          dragging = false;
          target = null;
        }}
      />
    </div>
  {/each}
</div>
