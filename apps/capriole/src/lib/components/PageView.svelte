<script lang="ts">
  // The page shown large, to annotate: the engine's drawing of it, and over it a layer in the page's
  // coordinates (points as shown — those of the text and of the annotations the engine lists) where the
  // pointer makes annotations with the tool chosen, or selects and moves them, or writes a line of the
  // page's own text again (edittext: a double click on a line, or on a paragraph laid out anew). Beside it, the selected
  // annotation (its text, colour, removal) and the list of the page's annotations (the keyboard's way).
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import FontsSection from './FontsSection.svelte';
  import FormLayer from './FormLayer.svelte';
  import LineEditor from './LineEditor.svelte';
  import ParagraphEditor from './ParagraphEditor.svelte';
  import TextBoxEditor from './TextBoxEditor.svelte';
  import type { AnnotationInfo, FieldInfo, PageRaster, PageText } from '@capriole/engine-client';
  import { tick } from 'svelte';
  import { colors, css, sameColor } from '$lib/colors';
  import { getEditor, type Rgb } from '$lib/editor.svelte';
  import { lineAt, paragraphAt } from '$lib/textedit';
  import { objectAt, objectName } from '$lib/objects';
  import { graphicSignatures } from '$lib/graphic-signatures.svelte';
  import { graphicSignatureWords } from '$lib/i18n/graphicSignatures';
  import { tr } from '$lib/i18n/index.svelte';
  import { pageViewWords } from '$lib/i18n/pageView';
  import { pageRange, pixelsPerPoint, zoomIn, zoomOut } from '$lib/zoom';

  const editor = getEditor();
  const t = $derived(tr(pageViewWords));
  const g = $derived(tr(graphicSignatureWords));
  const index = $derived(editor.viewing ?? 0);

  // The text gives the page's size as shown, the words that text markup goes on and the lines edittext
  // writes again — in the content's order, or as laid out for edittext (as the engine groups lines: text
  // an earlier edit drew at the end of the content is on its line) —, read again after each edit (the
  // page's text may have changed), the last one read kept until then (no flash).
  const order = $derived(editor.tool === 'edittext' ? 'layout' : 'content');
  const text = $derived.by(() => {
    void editor.version;
    return editor.engine.pageText(index, order);
  });
  let read = $state.raw<{ index: number; page: PageText; from: Promise<PageText> } | null>(null);
  let failure = $state<string | null>(null);
  $effect(() => {
    const reading = text;
    const at = index;
    let current = true;
    reading.then(
      (page) => {
        if (!current) return;
        read = { index: at, page, from: reading };
        failure = null;
      },
      (error: unknown) => {
        if (current) failure = error instanceof Error ? error.message : String(error);
      },
    );
    return () => {
      current = false;
    };
  });
  const page = $derived(read?.index === index ? read.page : null);
  /** The text shown is the page's now (not the one before an edit, still being read again). */
  const fresh = $derived(read !== null && read.from === text);
  /** edittext: the line (or paragraph) under the pointer, and its box. */
  let hovered = $state<number | null>(null);
  const hoveredBox = $derived(
    editor.tool === 'edittext' && hovered !== null && page
      ? ((editor.editMode === 'paragraph' ? page.blocks[hovered] : page.lines[hovered]) ?? null)
      : null,
  );
  /** Under a point: the line, or the paragraph (edittext's mode). */
  const textAt = (page: PageText, p: Point) =>
    editor.editMode === 'paragraph' ? paragraphAt(page, p[0], p[1]) : lineAt(page, p[0], p[1]);
  // Sharp enough for a page on a screen, fitted; zoomed in, as sharp as the zoom asks, within a size a
  // drawing may have (16 million pixels: an A4 page at about 600 dpi). Drawn again after each edit (the
  // annotations are in it).
  const density = Math.min(globalThis.devicePixelRatio || 1, 2);
  const fitDpi = Math.round(Math.min(220, 110 * density));
  const dpi = $derived.by(() => {
    if (editor.pageZoom === 'fit' || !page) return fitDpi;
    const wanted = (96 * editor.pageZoom * density) / 100;
    const most = Math.sqrt(16_000_000 / ((page.width / 72) * (page.height / 72)));
    return Math.round(Math.min(Math.max(wanted, fitDpi), most));
  });
  /** The page's width on the screen: the whole page in the window (fit), or its size at the zoom. */
  const pageWidth = $derived(
    !page
      ? '100%'
      : editor.pageZoom === 'fit'
        ? `min(100%, calc((100dvh - 7.5rem) * ${page.width / page.height}))`
        : `${(page.width * pixelsPerPoint * editor.pageZoom) / 100}px`,
  );

  /** Ctrl/Cmd + wheel zooms (not the browser's zoom). */
  function wheelZoom(node: HTMLElement) {
    const wheel = (e: WheelEvent) => {
      if (!(e.ctrlKey || e.metaKey) || e.deltaY === 0) return;
      e.preventDefault();
      const now = editor.pageZoomShown;
      editor.pageZoom = e.deltaY < 0 ? zoomIn(now, pageRange) : zoomOut(now, pageRange);
    };
    node.addEventListener('wheel', wheel, { passive: false });
    return () => node.removeEventListener('wheel', wheel);
  }
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
  type Corner = 0 | 1 | 2 | 3; // top left, top right, bottom right, bottom left
  type Drag =
    | { kind: 'move'; ref: [number, number]; start: Point; current: Point }
    | { kind: 'object'; start: Point; current: Point }
    | { kind: 'resize'; corner: Corner; start: Point; current: Point; stamp?: [number, number] }
    | { kind: 'markup'; from: number; to: number }
    | { kind: 'ink'; points: Point[] }
    | { kind: 'shape'; start: Point; current: Point };
  let drag = $state.raw<Drag | null>(null);

  function pointOf(event: MouseEvent): Point {
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
    // While a text is written, a click elsewhere only ends it (the box's blur).
    if (event.button !== 0 || editor.tool === 'form' || editor.tool === 'edittext' || editor.typing) return;
    // Text can be started while the last edit is written (the box does not touch the engine, and its
    // text joins the queue of edits): a click right after the last text is not lost.
    if (editor.busy && editor.tool !== 'freetext') return;
    const p = pointOf(event);
    const tool = editor.tool;
    if (tool === 'freetext') {
      // On a free text: write it again, in place.
      const text = freeTextAt(p);
      if (text) {
        event.preventDefault();
        editor.retype(text);
        return;
      }
    }
    if (tool === 'select') {
      // A corner of the object, or of the stamp, selected: resized; else an annotation (over the page), else
      // an object.
      const object = editor.object;
      const stamp = selectedStamp;
      const corner = object ? cornerAt(object.box, p) : stamp ? cornerAt(stamp.rect, p) : null;
      const hit =
        corner === null
          ? [...editor.annotations].reverse().find((a) => a.editable && a.ref && inside(p, a.rect, 3))
          : undefined;
      if (corner === null || object) editor.selectedAnnotation = hit?.ref ?? null;
      if (corner !== null) {
        drag = { kind: 'resize', corner, start: p, current: p, stamp: object ? undefined : stamp?.ref };
      } else if (hit?.ref) {
        editor.selectedObject = null;
        drag = { kind: 'move', ref: hit.ref, start: p, current: p };
      } else {
        editor.selectedObject = objectAt(editor.elements, p[0], p[1]);
        if (editor.selectedObject !== null) drag = { kind: 'object', start: p, current: p };
      }
    } else if (tool === 'highlight' || tool === 'underline' || tool === 'strikeout' || tool === 'squiggly') {
      const word = wordAt(page, p);
      if (word !== null) drag = { kind: 'markup', from: word, to: word };
    } else if (tool === 'redact') {
      // On text: the words dragged over; elsewhere: a rectangle.
      const word = wordAt(page, p);
      drag = word !== null ? { kind: 'markup', from: word, to: word } : { kind: 'shape', start: p, current: p };
    } else if (tool === 'ink') {
      drag = { kind: 'ink', points: [p] };
    } else if (tool === 'stamp' && !graphicSignatures.chosen) {
      // No graphic signature kept yet: where they are added.
      editor.settingsOpen = 'signatures';
      return;
    } else if (tool === 'note') {
      editor.annotate({ kind: 'note', rect: [p[0], p[1], p[0] + 20, p[1] + 20], color: editor.color, contents: '' });
      return;
    } else {
      drag = { kind: 'shape', start: p, current: p };
    }
    (event.currentTarget as Element).setPointerCapture(event.pointerId);
  }

  /** The corner of a box under a point (within a few points), if any. */
  function cornerAt(b: readonly number[], p: Point): Corner | null {
    const corners: Point[] = [
      [b[0]!, b[1]!],
      [b[2]!, b[1]!],
      [b[2]!, b[3]!],
      [b[0]!, b[3]!],
    ];
    const at = corners.findIndex((c) => Math.abs(c[0] - p[0]) <= kHandle && Math.abs(c[1] - p[1]) <= kHandle);
    return at >= 0 ? (at as Corner) : null;
  }

  /** How far from a corner of an object selected it can be taken to resize it (points as shown). */
  const kHandle = 5;

  /** The stamp selected (a graphic signature the editor made, or any stamp it can change): resizable. */
  const selectedStamp = $derived(
    editor.tool === 'select' && editor.annotation?.subtype === 'Stamp' && editor.annotation.ref
      ? (editor.annotation as AnnotationInfo & { ref: [number, number] })
      : null,
  );

  /**
   * Where the graphic signature chosen goes: in the box drawn, as large as it fits with its proportions,
   * centred; for a click, 150 points wide (at most 40% of the page) centred on it — within the page.
   */
  function stampRect(
    page: PageText,
    at: Point,
    drawn: [number, number, number, number],
  ): [number, number, number, number] | null {
    const signature = graphicSignatures.chosen;
    if (!signature) return null;
    const aspect = signature.width / signature.height;
    const [x0, y0, x1, y1] = drawn;
    let width: number;
    let centre: Point;
    if (x1 - x0 > 10 && y1 - y0 > 10) {
      width = Math.min(x1 - x0, (y1 - y0) * aspect);
      centre = [(x0 + x1) / 2, (y0 + y1) / 2];
    } else {
      width = Math.min(150, page.width * 0.4, page.height * 0.4 * aspect);
      centre = at;
    }
    const height = width / aspect;
    const left = Math.min(Math.max(centre[0] - width / 2, 0), Math.max(page.width - width, 0));
    const top = Math.min(Math.max(centre[1] - height / 2, 0), Math.max(page.height - height, 0));
    return [left, top, left + width, top + height];
  }

  /** An object's box while dragged: moved, or with a corner where the pointer is (the opposite one kept). */
  function draggedBox(b: readonly number[], d: Drag): [number, number, number, number] {
    const [x0, y0, x1, y1] = b as [number, number, number, number];
    if (d.kind === 'object') {
      const dx = d.current[0] - d.start[0];
      const dy = d.current[1] - d.start[1];
      return [x0 + dx, y0 + dy, x1 + dx, y1 + dy];
    }
    if (d.kind !== 'resize') return [x0, y0, x1, y1];
    if (d.stamp) {
      // A stamp keeps its proportions: from the opposite corner, the corner dragged follows the pointer
      // along the box's diagonal (the pointer projected on it), at least 8 points wide.
      const [ax, ay] = [d.corner === 1 || d.corner === 2 ? x0 : x1, d.corner >= 2 ? y0 : y1];
      const sx = d.corner === 1 || d.corner === 2 ? 1 : -1;
      const sy = d.corner >= 2 ? 1 : -1;
      const [w, h] = [x1 - x0, Math.max(y1 - y0, 0.01)];
      const along = ((d.current[0] - ax) * sx * w + (d.current[1] - ay) * sy * h) / (w * w + h * h);
      const scale = Math.max(along, 8 / w);
      return box([ax, ay], [ax + sx * w * scale, ay + sy * h * scale]);
    }
    const [cx, cy] = d.current;
    const moving: [number, number, number, number] =
      d.corner === 0
        ? [cx, cy, x1, y1]
        : d.corner === 1
          ? [x0, cy, cx, y1]
          : d.corner === 2
            ? [x0, y0, cx, cy]
            : [cx, y0, x1, cy];
    return box([moving[0], moving[1]], [moving[2], moving[3]]);
  }

  /** The topmost free text under a point (its reference). */
  function freeTextAt(p: Point): [number, number] | null {
    const hit = [...editor.annotations]
      .reverse()
      .find((a) => a.subtype === 'FreeText' && a.editable && a.ref && inside(p, a.rect, 2));
    return hit?.ref ?? null;
  }

  /** The height of one line of free text in its box (the engine's layout: 1.2 × the size, 2 points around). */
  const lineBox = (size: number) => size * 1.2 + 4;

  function move(event: PointerEvent, page: PageText) {
    if (editor.tool === 'edittext') {
      const p = pointOf(event);
      hovered = editor.editingLine || editor.editingParagraph ? null : textAt(page, p);
    }
    if (!drag) return;
    const p = pointOf(event);
    if (drag.kind === 'move' || drag.kind === 'shape' || drag.kind === 'object' || drag.kind === 'resize')
      drag = { ...drag, current: p };
    else if (drag.kind === 'ink') drag = { ...drag, points: [...drag.points, p] };
    else {
      const word = wordAt(page, p);
      if (word !== null) drag = { ...drag, to: word };
    }
  }

  function up(page: PageText) {
    const done = drag;
    drag = null;
    if (!done) return;
    const color = editor.color;
    const tool = editor.tool;
    if (done.kind === 'move') {
      const dx = done.current[0] - done.start[0];
      const dy = done.current[1] - done.start[1];
      if (Math.hypot(dx, dy) > 0.5) editor.changeAnnotation(done.ref, { moveBy: [dx, dy] });
    } else if (done.kind === 'object') {
      const dx = done.current[0] - done.start[0];
      const dy = done.current[1] - done.start[1];
      if (Math.hypot(dx, dy) > 0.5) editor.moveObject(dx, dy);
    } else if (done.kind === 'resize' && done.stamp) {
      const stamp = selectedStamp;
      if (!stamp || Math.hypot(done.current[0] - done.start[0], done.current[1] - done.start[1]) <= 0.5) return;
      editor.changeAnnotation(done.stamp, { rect: draggedBox(stamp.rect, done) });
    } else if (done.kind === 'resize') {
      const object = editor.object;
      if (!object) return;
      const to = draggedBox(object.box, done);
      if (
        to[2] - to[0] >= 2 &&
        to[3] - to[1] >= 2 &&
        Math.hypot(done.current[0] - done.start[0], done.current[1] - done.start[1]) > 0.5
      )
        editor.resizeObject(to);
    } else if (done.kind === 'markup' && tool === 'redact') {
      const quads = quadsOf(page, done.from, done.to);
      if (quads.length > 0) editor.annotate({ kind: 'redact', quads, color: kMark, fill: [0, 0, 0] });
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
      } else if (tool === 'sign') {
        if (x1 - x0 > 10 && y1 - y0 > 10) editor.requestSignature(index, [x0, y0, x1, y1]);
      } else if (tool === 'stamp') {
        const rect = stampRect(page, done.start, [x0, y0, x1, y1]);
        if (rect) editor.addStamp(rect);
      } else if (tool === 'redact') {
        if (x1 - x0 > 3 && y1 - y0 > 3)
          editor.annotate({ kind: 'redact', rect: [x0, y0, x1, y1], color: kMark, fill: [0, 0, 0] });
      } else if (tool === 'freetext') {
        const line = lineBox(editor.fontSize);
        if (x1 - x0 > 6) {
          // A rectangle: its width, at least one line high.
          editor.startTyping([x0, y0, x1, Math.max(y1, y0 + line)], 'wrap');
        } else {
          // A click: one line, the cursor where the click was, widening as typed.
          const [x, y] = done.start;
          editor.startTyping([x - 2, y - line / 2, x + editor.fontSize, y + line / 2], 'grow');
        }
      } else if (x1 - x0 > 3 && y1 - y0 > 3) {
        if (tool === 'square' || tool === 'circle')
          editor.annotate({ kind: tool, rect: [x0, y0, x1, y1], color, width: 2 });
      }
    }
  }

  /** The colour of redaction marks: their outline until applied. */
  const kMark: [number, number, number] = [0.85, 0, 0];
  /** The colour of what is being drawn. */
  /** The colour of a signature's box being drawn. */
  const kSignatureBox: [number, number, number] = [0.18, 0.36, 0.92];
  const previewColor = $derived(
    editor.tool === 'redact' ? kMark : editor.tool === 'sign' ? kSignatureBox : editor.color,
  );

  const nameOf = (a: AnnotationInfo) => t.names[a.subtype] ?? a.subtype;
  // Form widgets belong to their fields (listed apart, filled with the form tool).
  const listed = $derived(editor.annotations.filter((a) => a.subtype !== 'Widget'));
  const pageFields = $derived(editor.fields.filter((f) => f.widgets.some((w) => w.page === index)));

  // The text of the selected annotation is written as it is typed: after a short pause (each pause one
  // step of undo), and when the box is left. The box keeps what is typed (defaultValue: listing the
  // annotations again after a write does not put back an older text).
  const kPause = 400;
  let pending: { ref: [number, number]; text: string; timer: ReturnType<typeof setTimeout> } | null = null;

  function typed(ref: [number, number], text: string) {
    if (pending) clearTimeout(pending.timer);
    pending = { ref, text, timer: setTimeout(written, kPause) };
  }

  /** Writes the text waiting, if any (also when its box goes: another annotation selected). */
  function written() {
    if (!pending) return;
    const { ref, text, timer } = pending;
    clearTimeout(timer);
    pending = null;
    const current = editor.annotations.find((a) => a.ref?.[0] === ref[0] && a.ref?.[1] === ref[1]);
    if (current && current.contents !== text) editor.changeAnnotation(ref, { contents: text });
  }

  /** A field's value in a few words, for the list. */
  function shown(f: FieldInfo): string {
    const display = (v: string) => f.options.find((o) => o.value === v)?.display ?? v;
    switch (f.type) {
      case 'text':
        return f.password && f.value[0] ? '••••' : (f.value[0] ?? '');
      case 'checkbox':
        return f.value[0] && f.value[0] !== 'Off' ? t.yes : t.no;
      case 'radio':
        return f.value[0] && f.value[0] !== 'Off' ? display(f.value[0]) : '';
      case 'signature':
        return t.signatureField;
      case 'pushbutton':
        return t.pushbutton;
      default:
        return f.value.map(display).join(', ');
    }
  }

  /** Goes to a field's control on the page. */
  async function focusField(f: FieldInfo) {
    editor.tool = 'form';
    await tick();
    document.querySelector<HTMLElement>(`[data-field="${f.ref[0]} ${f.ref[1]}"]`)?.focus();
  }
  const same = (a: AnnotationInfo, ref: [number, number] | null) =>
    ref !== null && a.ref?.[0] === ref[0] && a.ref?.[1] === ref[1];
  const cursor = $derived(
    editor.tool === 'select'
      ? 'default'
      : ['highlight', 'underline', 'strikeout', 'squiggly', 'redact', 'edittext'].includes(editor.tool)
        ? 'text'
        : 'crosshair',
  );
</script>

<svelte:window
  onkeydown={(e) => {
    // Delete: the object selected goes (not while a box of text has the keys).
    const target = e.target as HTMLElement | null;
    if (
      (e.key === 'Delete' || e.key === 'Backspace') &&
      editor.tool === 'select' &&
      editor.selectedObject !== null &&
      !editor.busy &&
      !(target && (target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName)))
    ) {
      e.preventDefault();
      editor.deleteObject();
    }
  }}
/>

<div class="flex min-h-0 flex-1">
  <main class="min-w-0 flex-1 overflow-auto bg-muted p-6" {@attach wheelZoom}>
    {#if failure}
      <p class="rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">{failure}</p>
    {:else if !page}
      <p class="text-sm text-muted-foreground">{t.reading}</p>
    {:else}
      <!-- Fitted: the whole page in the window, as wide as the room or as the height allows. Zoomed: its
           size at the zoom, scrolled when larger than the window. -->
      <div
        class="@container relative mx-auto bg-white ring-1 ring-border"
        style:aspect-ratio="{page.width} / {page.height}"
        style:width={pageWidth}
        data-testid="page-sheet"
        bind:clientWidth={
          () => 0,
          (width) => {
            if (width > 0) editor.pageZoomShown = (width / (page.width * pixelsPerPoint)) * 100;
          }
        }
      >
        <canvas {@attach paint} class="absolute inset-0 size-full" aria-hidden="true"></canvas>
        <svg
          viewBox="0 0 {page.width} {page.height}"
          class="absolute inset-0 size-full touch-none select-none"
          style:cursor
          role="application"
          aria-label={t.surface(
            index + 1,
            editor.tool === 'select' || editor.tool === 'form' || editor.tool === 'edittext' ? editor.tool : 'draw',
          )}
          data-testid="page-surface"
          onpointerdown={(e) => down(e, page)}
          onpointermove={(e) => move(e, page)}
          onpointerup={() => up(page)}
          onpointercancel={() => (drag = null)}
          onpointerleave={() => (hovered = null)}
          ondblclick={(e) => {
            const p = pointOf(e);
            if (editor.tool === 'edittext') {
              // A double click on a line (or paragraph) of the page's text writes it again, in place.
              const hit = textAt(page, p);
              // Only on the text as it is now: a line read before the last edit would be replaced wrongly.
              if (hit !== null && !editor.busy && fresh) {
                hovered = null;
                if (editor.editMode === 'paragraph') editor.startEditingParagraph(page, hit);
                else editor.startEditingLine(page, hit);
              }
              return;
            }
            // A double click on a free text writes it again, in place (with any tool but the form's).
            const text = editor.tool !== 'form' && !editor.typing ? freeTextAt(p) : null;
            if (text) editor.retype(text);
          }}
        >
          {#if hoveredBox}
            {@const l = hoveredBox}
            <rect
              x={l[0] - 1}
              y={l[1] - 1}
              width={l[2] - l[0] + 2}
              height={l[3] - l[1] + 2}
              fill="transparent"
              class="stroke-emphasis"
              stroke-width="1"
              stroke-dasharray="3 2"
              vector-effect="non-scaling-stroke"
              pointer-events="none"
              data-testid="line-hover"
            />
          {/if}
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
                  'stroke-emphasis',
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
          {#if editor.tool === 'select' && editor.object}
            {@const o =
              drag?.kind === 'object' || drag?.kind === 'resize'
                ? draggedBox(editor.object.box, drag)
                : editor.object.box}
            <rect
              x={o[0]}
              y={o[1]}
              width={o[2] - o[0]}
              height={o[3] - o[1]}
              fill="transparent"
              class="stroke-emphasis"
              stroke-width="1"
              stroke-dasharray="3 2"
              vector-effect="non-scaling-stroke"
              pointer-events="none"
              data-testid="object-selected"
            />
            {#each [[o[0], o[1]], [o[2], o[1]], [o[2], o[3]], [o[0], o[3]]] as const as [cx, cy], i (i)}
              <rect
                x={cx - 2.5}
                y={cy - 2.5}
                width="5"
                height="5"
                class="fill-background stroke-emphasis"
                stroke-width="1"
                vector-effect="non-scaling-stroke"
                style:cursor={i % 2 === 0 ? 'nwse-resize' : 'nesw-resize'}
                data-testid="object-handle"
              />
            {/each}
          {/if}
          {#if selectedStamp}
            {@const r =
              drag?.kind === 'resize' && drag.stamp ? draggedBox(selectedStamp.rect, drag) : selectedStamp.rect}
            {#if drag?.kind === 'resize' && drag.stamp}
              <rect
                x={r[0]}
                y={r[1]}
                width={r[2] - r[0]}
                height={r[3] - r[1]}
                fill="transparent"
                class="stroke-emphasis"
                stroke-width="1"
                stroke-dasharray="3 2"
                vector-effect="non-scaling-stroke"
                pointer-events="none"
              />
            {/if}
            {#each [[r[0], r[1]], [r[2], r[1]], [r[2], r[3]], [r[0], r[3]]] as const as [cx, cy], i (i)}
              <rect
                x={cx - 2.5}
                y={cy - 2.5}
                width="5"
                height="5"
                class="fill-background stroke-emphasis"
                stroke-width="1"
                vector-effect="non-scaling-stroke"
                style:cursor={i % 2 === 0 ? 'nwse-resize' : 'nesw-resize'}
                data-testid="stamp-handle"
              />
            {/each}
          {/if}
          {#if drag?.kind === 'markup'}
            {#each quadsOf(page, drag.from, drag.to) as q, i (i)}
              <rect
                x={q[0]}
                y={q[1]}
                width={q[2] - q[0]}
                height={q[3] - q[1]}
                fill={css(previewColor)}
                opacity="0.35"
              />
            {/each}
          {:else if drag?.kind === 'ink'}
            <polyline
              points={drag.points.map((p) => p.join(',')).join(' ')}
              fill="none"
              stroke={css(previewColor)}
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
                stroke={css(previewColor)}
                stroke-width="2"
              />
            {:else if editor.tool === 'circle'}
              <ellipse
                cx={(r[0] + r[2]) / 2}
                cy={(r[1] + r[3]) / 2}
                rx={(r[2] - r[0]) / 2}
                ry={(r[3] - r[1]) / 2}
                fill="none"
                stroke={css(previewColor)}
                stroke-width="2"
              />
            {:else}
              <rect
                x={r[0]}
                y={r[1]}
                width={r[2] - r[0]}
                height={r[3] - r[1]}
                fill="none"
                stroke={css(previewColor)}
                stroke-width="2"
              />
            {/if}
          {/if}
        </svg>
        {#if editor.typing}
          {#key editor.typing}
            <TextBoxEditor typing={editor.typing} width={page.width} height={page.height} />
          {/key}
        {/if}
        {#if editor.editingLine}
          {#key editor.editingLine}
            <LineEditor editing={editor.editingLine} width={page.width} height={page.height} />
          {/key}
        {/if}
        {#if editor.editingParagraph}
          {#key editor.editingParagraph}
            <ParagraphEditor editing={editor.editingParagraph} width={page.width} height={page.height} />
          {/key}
        {/if}
        {#if editor.tool === 'form'}
          <FormLayer width={page.width} height={page.height} />
        {/if}
      </div>
    {/if}
  </main>

  <aside
    class="flex w-72 shrink-0 flex-col gap-4 overflow-y-auto border-s bg-card p-4 text-sm"
    aria-label={t.annotations}
  >
    {#if editor.tool === 'select' && editor.object && !editor.annotation}
      {@const o = editor.object}
      <section class="flex flex-col gap-3" data-testid="object-panel">
        <h2 class="font-semibold">{objectName(o)}</h2>
        <p class="text-muted-foreground">
          {t.objectHelp}
        </p>
        <button
          type="button"
          class="flex items-center justify-center gap-1.5 rounded-md border px-2.5 py-1.5 text-danger hover:bg-danger-background disabled:opacity-60"
          disabled={editor.busy}
          onclick={() => editor.deleteObject()}><Trash2 class="size-4" /> {t.delete}</button
        >
      </section>
    {:else if editor.annotation}
      {@const a = editor.annotation}
      {@const ref = a.ref!}
      <section class="flex flex-col gap-3" data-testid="annotation-panel">
        <h2 class="font-semibold">{nameOf(a)}</h2>
        {#if a.editable}
          {#key `${ref[0]} ${ref[1]}`}
            <label class="flex flex-col gap-1">
              <span class="text-muted-foreground">{a.subtype === 'FreeText' ? t.text : t.comment}</span>
              <textarea
                class="min-h-24 rounded-md border bg-card px-2 py-1.5 text-base focus:border-emphasis focus:ring-3 focus:ring-accent focus:outline-none sm:text-sm"
                defaultValue={a.contents}
                oninput={(e) => typed(ref, e.currentTarget.value)}
                onchange={() => written()}
                {@attach () => written}></textarea>
            </label>
          {/key}
          <!-- A stamp's colours are its picture's. -->
          {#if a.subtype !== 'Stamp'}
            <div class="flex items-center gap-1" role="radiogroup" aria-label={t.annotationColor}>
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
          {/if}
          <button
            type="button"
            class="flex items-center justify-center gap-1.5 rounded-md border px-2.5 py-1.5 text-danger hover:bg-danger-background"
            onclick={() => editor.removeAnnotation(ref)}><Trash2 class="size-4" /> {t.delete}</button
          >
        {/if}
      </section>
    {:else}
      <p class="text-muted-foreground">
        {editor.tool === 'edittext'
          ? editor.editMode === 'paragraph'
            ? t.helpParagraph
            : t.helpLine
          : editor.tool === 'sign'
            ? t.helpSign
            : editor.tool === 'stamp'
              ? `${g.place} ${g.about}`
              : editor.tool === 'redact'
                ? t.helpRedact
                : editor.tool === 'form'
                  ? t.helpForm
                  : editor.tool === 'select'
                    ? t.helpSelect
                    : t.helpDraw}
      </p>
      {#if editor.tool === 'edittext'}
        <div class="flex gap-0.5 rounded-lg bg-muted p-1" role="radiogroup" aria-label={t.editWhat}>
          {#each ['line', 'paragraph'] as const as mode (mode)}
            <button
              type="button"
              role="radio"
              aria-checked={editor.editMode === mode}
              class={[
                'flex-1 rounded px-2 py-1',
                editor.editMode === mode
                  ? 'bg-card font-medium shadow-xs ring-1 ring-border'
                  : 'text-muted-foreground hover:text-foreground',
              ]}
              onclick={() => {
                editor.editMode = mode;
                hovered = null;
              }}>{t[mode]}</button
            >
          {/each}
        </div>
      {/if}
      {#if editor.tool === 'edittext' && editor.noRoom}
        {@const room = editor.noRoom}
        <section
          class="flex flex-col gap-2 rounded-md bg-warning-background px-3 py-2 text-warning"
          data-testid="no-room"
        >
          <p>{t.noRoom(room.needed, room.fit)}</p>
          <div class="flex flex-wrap gap-2">
            {#if !room.cannotMove}
              <button
                type="button"
                class="rounded-md border border-current px-2.5 py-1 font-medium disabled:opacity-60"
                disabled={editor.busy}
                onclick={() => editor.moveFollowing()}>{t.moveFollowing}</button
              >
            {/if}
            <button
              type="button"
              class="rounded-md border border-current px-2.5 py-1 font-medium disabled:opacity-60"
              disabled={editor.busy}
              onclick={() => editor.shrinkParagraph()}>{t.shrink}</button
            >
            <button type="button" class="rounded-md px-2.5 py-1 underline" onclick={() => editor.backToParagraph()}
              >{t.backToText}</button
            >
          </div>
        </section>
      {/if}
      {#if editor.tool === 'edittext' && editor.textNotice}
        <p class="rounded-md bg-warning-background px-3 py-2 text-warning" role="status" data-testid="text-edit-notice">
          {editor.textNotice}
        </p>
      {/if}
      {#if editor.tool === 'edittext'}
        <FontsSection />
      {/if}
    {/if}
    {#if pageFields.length > 0}
      <section class="flex flex-col gap-1">
        <h2 class="font-semibold">{t.formFields}</h2>
        <ul class="flex flex-col gap-1" data-testid="field-list">
          {#each pageFields as f (`${f.ref[0]} ${f.ref[1]}`)}
            <li>
              <button
                type="button"
                class="flex w-full flex-col rounded-md px-2 py-1 text-start hover:bg-muted disabled:opacity-60"
                disabled={!f.fillable}
                onclick={() => focusField(f)}
              >
                <span class="truncate"
                  >{f.label || f.name || t.field}{f.required ? ' *' : ''}{f.readOnly ? t.readOnly : ''}</span
                >
                <span class="truncate text-muted-foreground">{shown(f) || '—'}</span>
              </button>
            </li>
          {/each}
        </ul>
      </section>
    {/if}
    <section class="flex flex-col gap-1">
      <h2 class="font-semibold">{t.pageAnnotations}</h2>
      {#if listed.length === 0}
        <p class="text-muted-foreground">{t.none}</p>
      {/if}
      <ul class="flex flex-col gap-1" data-testid="annotation-list">
        {#each listed as a, i (a.ref ? `${a.ref[0]} ${a.ref[1]}` : `d${i}`)}
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
