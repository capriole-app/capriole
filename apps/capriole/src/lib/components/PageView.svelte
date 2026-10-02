<script lang="ts">
  // The page shown large, to annotate: the engine's drawing of it, and over it a layer in the page's
  // coordinates (points as shown — those of the text and of the annotations the engine lists) where the
  // pointer makes annotations with the tool chosen, or selects and moves them. Beside it, the selected
  // annotation (its text, colour, removal) and the list of the page's annotations (the keyboard's way).
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import type { AnnotationInfo, PageRaster, PageText } from '@capriole/engine-client';
  import { colors, css, sameColor } from '$lib/colors';
  import { getEditor, type Rgb } from '$lib/editor.svelte';

  const editor = getEditor();
  const index = $derived(editor.viewing ?? 0);

  // The text gives the page's size as shown, and the words that text markup goes on.
  const text = $derived(editor.engine.pageText(index, 'content'));
  // Sharp enough for a page on a screen; drawn again after each edit (the annotations are in it).
  const dpi = Math.round(Math.min(220, 110 * Math.min(globalThis.devicePixelRatio || 1, 2)));
  const raster = $derived.by(() => {
    void editor.version;
    return editor.engine.pageRender(index, dpi);
  });

  /** Paints the drawing once it arrives, keeping the previous one until then (no flash on each edit). */
  function paint(canvas: HTMLCanvasElement) {
    let current = true;
    raster.then((image: PageRaster) => {
      if (!current) return;
      canvas.width = image.width;
      canvas.height = image.height;
      canvas
        .getContext('2d')
        ?.putImageData(new ImageData(new Uint8ClampedArray(image.pixels), image.width, image.height), 0, 0);
    });
    return () => {
      current = false;
    };
  }

  type Point = [number, number];
  type Drag =
    | { kind: 'move'; ref: [number, number]; start: Point; current: Point }
    | { kind: 'markup'; from: number; to: number }
    | { kind: 'ink'; points: Point[] }
    | { kind: 'shape'; start: Point; current: Point };
  let drag = $state.raw<Drag | null>(null);

  function pointOf(event: PointerEvent): Point {
    const svg = event.currentTarget as SVGSVGElement;
    const matrix = svg.getScreenCTM()?.inverse();
    const p = new DOMPoint(event.clientX, event.clientY).matrixTransform(matrix);
    return [p.x, p.y];
  }

  const inside = (p: Point, r: readonly number[], margin = 0) =>
    p[0] >= r[0]! - margin && p[0] <= r[2]! + margin && p[1] >= r[1]! - margin && p[1] <= r[3]! + margin;

  /** The word under the pointer (or within a few points of it). */
  function wordAt(page: PageText, p: Point): number | null {
    const hit = page.words.findIndex((w) => inside(p, w, 2));
    return hit >= 0 ? hit : null;
  }

  /** The boxes text markup covers: per line, the words of the range on it, as one box. */
  function quadsOf(page: PageText, from: number, to: number): [number, number, number, number][] {
    const [a, b] = from <= to ? [from, to] : [to, from];
    const quads: [number, number, number, number][] = [];
    for (const [, , , , first, count] of page.lines) {
      const lo = Math.max(a, first);
      const hi = Math.min(b, first + count - 1);
      if (lo > hi) continue;
      const boxes = page.words.slice(lo, hi + 1);
      quads.push([
        Math.min(...boxes.map((w) => w[0])),
        Math.min(...boxes.map((w) => w[1])),
        Math.max(...boxes.map((w) => w[2])),
        Math.max(...boxes.map((w) => w[3])),
      ]);
    }
    return quads;
  }

  const box = (a: Point, b: Point): [number, number, number, number] => [
    Math.min(a[0], b[0]),
    Math.min(a[1], b[1]),
    Math.max(a[0], b[0]),
    Math.max(a[1], b[1]),
  ];

  function down(event: PointerEvent, page: PageText) {
    console.log('down', event.button, editor.busy, editor.tool);
    if (event.button !== 0 || editor.busy) return;
    const p = pointOf(event);
    const tool = editor.tool;
    if (tool === 'select') {
      const hit = [...editor.annotations].reverse().find((a) => a.editable && a.ref && inside(p, a.rect, 3));
      editor.selectedAnnotation = hit?.ref ?? null;
      if (hit?.ref) drag = { kind: 'move', ref: hit.ref, start: p, current: p };
    } else if (tool === 'highlight' || tool === 'underline' || tool === 'strikeout' || tool === 'squiggly') {
      const word = wordAt(page, p);
      if (word !== null) drag = { kind: 'markup', from: word, to: word };
    } else if (tool === 'ink') {
      drag = { kind: 'ink', points: [p] };
    } else if (tool === 'note') {
      editor.annotate({ kind: 'note', rect: [p[0], p[1], p[0] + 20, p[1] + 20], color: editor.color, contents: '' });
      return;
    } else {
      drag = { kind: 'shape', start: p, current: p };
    }
    (event.currentTarget as Element).setPointerCapture(event.pointerId);
  }

  function move(event: PointerEvent, page: PageText) {
    if (!drag) return;
    const p = pointOf(event);
    if (drag.kind === 'move' || drag.kind === 'shape') drag = { ...drag, current: p };
    else if (drag.kind === 'ink') drag = { ...drag, points: [...drag.points, p] };
    else {
      const word = wordAt(page, p);
      if (word !== null) drag = { ...drag, to: word };
    }
  }

  function up(page: PageText) {
    const done = drag;
    console.log('up', JSON.stringify(done), editor.tool, editor.busy);
    drag = null;
    if (!done) return;
    const color = editor.color;
    const tool = editor.tool;
    if (done.kind === 'move') {
      const dx = done.current[0] - done.start[0];
      const dy = done.current[1] - done.start[1];
      if (Math.hypot(dx, dy) > 0.5) editor.changeAnnotation(done.ref, { moveBy: [dx, dy] });
    } else if (done.kind === 'markup' && tool !== 'select' && tool !== 'note') {
      const quads = quadsOf(page, done.from, done.to);
      if (quads.length > 0) editor.annotate({ kind: tool as 'highlight', quads, color });
    } else if (done.kind === 'ink') {
      if (done.points.length >= 2) editor.annotate({ kind: 'ink', strokes: [done.points], color, width: 2 });
    } else if (done.kind === 'shape') {
      const [x0, y0, x1, y1] = box(done.start, done.current);
      if (tool === 'line') {
        if (Math.hypot(x1 - x0, y1 - y0) > 3)
          editor.annotate({ kind: 'line', from: done.start, to: done.current, color, width: 2 });
      } else if (x1 - x0 > 3 && y1 - y0 > 3) {
        if (tool === 'freetext')
          editor.annotate({ kind: 'freetext', rect: [x0, y0, x1, y1], color, contents: 'Testo', fontSize: 12 });
        else if (tool === 'square' || tool === 'circle')
          editor.annotate({ kind: tool, rect: [x0, y0, x1, y1], color, width: 2 });
      }
    }
  }

  const names: Record<string, string> = {
    Highlight: 'Evidenziazione',
    Underline: 'Sottolineatura',
    StrikeOut: 'Barrato',
    Squiggly: 'Ondulato',
    Text: 'Nota',
    Ink: 'Disegno',
    Square: 'Rettangolo',
    Circle: 'Ellisse',
    Line: 'Linea',
    FreeText: 'Testo',
    Link: 'Collegamento',
    Widget: 'Campo modulo',
    Stamp: 'Timbro',
  };
  const nameOf = (a: AnnotationInfo) => names[a.subtype] ?? a.subtype;
  const same = (a: AnnotationInfo, ref: [number, number] | null) =>
    ref !== null && a.ref?.[0] === ref[0] && a.ref?.[1] === ref[1];
  const cursor = $derived(
    editor.tool === 'select'
      ? 'default'
      : ['highlight', 'underline', 'strikeout', 'squiggly'].includes(editor.tool)
        ? 'text'
        : 'crosshair',
  );
</script>

<div class="flex min-h-0 flex-1">
  <main class="min-w-0 flex-1 overflow-auto bg-muted p-6">
    {#await text}
      <p class="text-sm text-muted-foreground">Lettura della pagina…</p>
    {:then page}
      <!-- The whole page in the window (fit to page): as wide as the room, or as the height allows. -->
      <div
        class="relative mx-auto bg-white shadow-sm"
        style:aspect-ratio="{page.width} / {page.height}"
        style:width="min(100%, calc((100dvh - 7.5rem) * {page.width / page.height}))"
      >
        <canvas {@attach paint} class="absolute inset-0 size-full" aria-hidden="true"></canvas>
        <svg
          viewBox="0 0 {page.width} {page.height}"
          class="absolute inset-0 size-full touch-none select-none"
          style:cursor
          role="application"
          aria-label="Pagina {index + 1}: {editor.tool === 'select' ? 'seleziona un’annotazione' : 'disegna'}"
          data-testid="page-surface"
          onpointerdown={(e) => down(e, page)}
          onpointermove={(e) => move(e, page)}
          onpointerup={() => up(page)}
          onpointercancel={() => (drag = null)}
        >
          {#each editor.annotations as a, i (a.ref ? `${a.ref[0]} ${a.ref[1]}` : `d${i}`)}
            {#if a.editable && !a.hidden}
              {@const moving = drag?.kind === 'move' && same(a, drag.ref) ? drag : null}
              <rect
                x={a.rect[0] + (moving ? moving.current[0] - moving.start[0] : 0)}
                y={a.rect[1] + (moving ? moving.current[1] - moving.start[1] : 0)}
                width={a.rect[2] - a.rect[0]}
                height={a.rect[3] - a.rect[1]}
                fill="transparent"
                class={[
                  'stroke-primary',
                  same(a, editor.selectedAnnotation) ? 'opacity-100' : 'opacity-0',
                  editor.tool === 'select' && 'hover:opacity-60',
                ]}
                stroke-width="1"
                stroke-dasharray="3 2"
                vector-effect="non-scaling-stroke"
                pointer-events={editor.tool === 'select' ? 'all' : 'none'}
                data-annotation={a.subtype}
              />
            {/if}
          {/each}
          {#if drag?.kind === 'markup'}
            {#each quadsOf(page, drag.from, drag.to) as q, i (i)}
              <rect
                x={q[0]}
                y={q[1]}
                width={q[2] - q[0]}
                height={q[3] - q[1]}
                fill={css(editor.color)}
                opacity="0.35"
              />
            {/each}
          {:else if drag?.kind === 'ink'}
            <polyline
              points={drag.points.map((p) => p.join(',')).join(' ')}
              fill="none"
              stroke={css(editor.color)}
              stroke-width="2"
              stroke-linecap="round"
              stroke-linejoin="round"
            />
          {:else if drag?.kind === 'shape'}
            {@const r = box(drag.start, drag.current)}
            {#if editor.tool === 'line'}
              <line
                x1={drag.start[0]}
                y1={drag.start[1]}
                x2={drag.current[0]}
                y2={drag.current[1]}
                stroke={css(editor.color)}
                stroke-width="2"
              />
            {:else if editor.tool === 'circle'}
              <ellipse
                cx={(r[0] + r[2]) / 2}
                cy={(r[1] + r[3]) / 2}
                rx={(r[2] - r[0]) / 2}
                ry={(r[3] - r[1]) / 2}
                fill="none"
                stroke={css(editor.color)}
                stroke-width="2"
              />
            {:else}
              <rect
                x={r[0]}
                y={r[1]}
                width={r[2] - r[0]}
                height={r[3] - r[1]}
                fill="none"
                stroke={css(editor.color)}
                stroke-width="2"
              />
            {/if}
          {/if}
        </svg>
      </div>
    {:catch error}
      <p class="rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">
        {error instanceof Error ? error.message : String(error)}
      </p>
    {/await}
  </main>

  <aside
    class="flex w-72 shrink-0 flex-col gap-4 overflow-y-auto border-s bg-card p-4 text-sm"
    aria-label="Annotazioni"
  >
    {#if editor.annotation}
      {@const a = editor.annotation}
      {@const ref = a.ref!}
      <section class="flex flex-col gap-3" data-testid="annotation-panel">
        <h2 class="font-semibold">{nameOf(a)}</h2>
        {#if a.editable}
          {#key `${ref[0]} ${ref[1]}`}
            <label class="flex flex-col gap-1">
              <span class="text-muted-foreground">{a.subtype === 'FreeText' ? 'Testo' : 'Commento'}</span>
              <textarea
                class="min-h-24 rounded-md border bg-background px-2 py-1.5 text-base"
                value={a.contents}
                onchange={(e) => editor.changeAnnotation(ref, { contents: e.currentTarget.value })}></textarea>
            </label>
          {/key}
          <div class="flex items-center gap-1" role="radiogroup" aria-label="Colore dell'annotazione">
            {#each colors as { name, rgb } (name)}
              <button
                type="button"
                role="radio"
                aria-checked={sameColor(a.color, rgb)}
                aria-label={name}
                title={name}
                class={[
                  'size-6 rounded-full border-2',
                  sameColor(a.color, rgb) ? 'border-foreground' : 'border-transparent',
                ]}
                style:background-color={css(rgb)}
                onclick={() => editor.changeAnnotation(ref, { color: [...rgb] as Rgb })}
              ></button>
            {/each}
          </div>
          <button
            type="button"
            class="flex items-center justify-center gap-1.5 rounded-md border px-2.5 py-1.5 text-danger hover:bg-danger-background"
            onclick={() => editor.removeAnnotation(ref)}><Trash2 class="size-4" /> Elimina</button
          >
        {/if}
      </section>
    {:else}
      <p class="text-muted-foreground">
        {editor.tool === 'select'
          ? 'Clic su un’annotazione per modificarla; trascinala per spostarla.'
          : 'Scegli il punto della pagina: trascina sul testo per evidenziare, sottolineare o barrare; trascina per disegnare; clic per una nota.'}
      </p>
    {/if}
    <section class="flex flex-col gap-1">
      <h2 class="font-semibold">Annotazioni della pagina</h2>
      {#if editor.annotations.length === 0}
        <p class="text-muted-foreground">Nessuna.</p>
      {/if}
      <ul class="flex flex-col gap-1" data-testid="annotation-list">
        {#each editor.annotations as a, i (a.ref ? `${a.ref[0]} ${a.ref[1]}` : `d${i}`)}
          <li>
            <button
              type="button"
              class={[
                'flex w-full items-center gap-2 rounded-md px-2 py-1 text-start hover:bg-muted disabled:opacity-60',
                same(a, editor.selectedAnnotation) && 'bg-accent',
              ]}
              disabled={!a.editable}
              onclick={() => {
                editor.tool = 'select';
                editor.selectedAnnotation = a.ref;
              }}
            >
              <span
                class="size-3 shrink-0 rounded-full border"
                style:background-color={a.color ? css(a.color) : 'transparent'}
              ></span>
              <span class="truncate">{nameOf(a)}{a.contents ? ` — ${a.contents}` : ''}</span>
            </button>
          </li>
        {/each}
      </ul>
    </section>
  </aside>
</div>
