// The page editor's state: the open file, its pages, the selection, and the edits — each a call to the
// engine (packages/engine-client), which changes the document in its worker; nothing leaves the browser.
import { createContext } from 'svelte';
import { SvelteSet } from 'svelte/reactivity';
import {
  EngineClient,
  EngineError,
  Status,
  type AnnotationChange,
  type AnnotationInfo,
  type AnnotationSpec,
  type FieldInfo,
  type PageRaster,
  type Repair,
  type Summary,
} from '@capriole/engine-client';
import { baseName, pdfDateNow } from './files';
import { fileName, fromBrowser, pickPdf, savePdf, type PickedFile } from './platform';

/** What a pointer does on the page shown large: fill the form, select (and move) annotations, or make one. */
export type Tool = 'form' | 'select' | AnnotationSpec['kind'];

export type Rgb = [number, number, number];

/** Undo keeps full copies of the document before each edit, within this many bytes (oldest dropped). */
const kHistoryBytes = 256 * 1024 * 1024;

export class Editor {
  readonly engine = new EngineClient();

  engineVersion = $state<string | null>(null);
  /** The open file's name; null when no file is open. */
  name = $state<string | null>(null);
  /** Desktop: where the open file lives ("Save" writes there); null in the browser or for a new file. */
  path = $state<string | null>(null);
  pages = $state(0);
  /** Goes up with every change of the document: the thumbnails draw again. */
  version = $state(0);
  readonly selected = new SvelteSet<number>();
  /** Where a Shift-click range starts. */
  anchor = $state<number | null>(null);
  busy = $state(false);
  error = $state<string | null>(null);
  /** What the engine repaired in the file when opening it (already written into the edited copy). */
  repairs = $state.raw<Repair[]>([]);
  /** Edited since opened or saved. */
  modified = $state(false);
  passwordNeeded = $state(false);
  passwordRejected = $state(false);
  canUndo = $state(false);

  /** The page shown large, to annotate; null: the grid of pages. */
  viewing = $state<number | null>(null);
  /** Its annotations, as the engine lists them. */
  annotations = $state.raw<AnnotationInfo[]>([]);
  /** The fields of the document's form, as the engine lists them (widgets on every page). */
  fields = $state.raw<FieldInfo[]>([]);
  /** The annotation selected on it (its reference). */
  selectedAnnotation = $state.raw<[number, number] | null>(null);
  tool = $state<Tool>('select');
  color = $state.raw<Rgb>([1, 0.82, 0]);

  #source: PickedFile | null = null;
  /** The page objects ("number generation"), in page order: what a thumbnail shows does not change when its page moves. */
  #refs: string[] = [];
  /** Thumbnails by page object and resolution: the bitmap once drawn, the promise while drawing. */
  #thumbnails = new Map<string, PageRaster | Promise<PageRaster>>();
  #password = '';
  #history: ArrayBuffer[] = [];
  /** The edit of the page shown being written (the next waits for it). */
  #queue: Promise<void> = Promise.resolve();

  constructor() {
    this.engine
      .version()
      .then((version) => (this.engineVersion = version))
      .catch(() => {});
  }

  /** The selected pages, in page order. */
  get selection(): number[] {
    return [...this.selected].sort((a, b) => a - b);
  }

  /** A file from the browser's input or a drop. */
  async openFile(file: File): Promise<void> {
    await this.open(await fromBrowser(file));
  }

  /** Desktop: a file chosen in the system's dialog. */
  async openFromDialog(): Promise<void> {
    const picked = await pickPdf().catch((error: unknown) => {
      this.error = describe(error);
      return null;
    });
    if (picked) await this.open(picked);
  }

  async open(source: PickedFile, password = ''): Promise<void> {
    this.#source = source;
    this.#password = password;
    await this.#run(async () => {
      try {
        // A copy goes to the engine (the bytes are transferred): asking for a password reads them again.
        const opened = await this.engine.openForEditing(source.data.slice(0), password);
        this.name = source.name;
        this.path = source.path;
        this.repairs = opened.repairs;
        this.#history = [];
        this.canUndo = false;
        this.passwordNeeded = false;
        this.passwordRejected = false;
        this.#show(opened.summary, []);
        this.modified = false;
      } catch (error) {
        if (error instanceof EngineError && error.status === Status.passwordRequired) {
          this.passwordRejected = this.passwordNeeded && password !== '';
          this.passwordNeeded = true;
          return;
        }
        throw error;
      }
    });
  }

  async submitPassword(password: string): Promise<void> {
    if (this.#source) await this.open(this.#source, password);
  }

  cancelPassword(): void {
    this.passwordNeeded = false;
    this.passwordRejected = false;
    this.#source = null;
  }

  // ── Selection ──────────────────────────────────────────────────────────────────────────────

  /** A click on a page: alone, added or removed (toggle: Ctrl/Cmd), or a range from the anchor (Shift). */
  select(page: number, how: { range?: boolean; toggle?: boolean } = {}): void {
    if (how.range && this.anchor !== null) {
      const [from, to] = this.anchor < page ? [this.anchor, page] : [page, this.anchor];
      if (!how.toggle) this.selected.clear();
      for (let p = from; p <= to; p++) this.selected.add(p);
      return;
    }
    if (how.toggle) {
      if (this.selected.has(page)) this.selected.delete(page);
      else this.selected.add(page);
    } else {
      this.selected.clear();
      this.selected.add(page);
    }
    this.anchor = page;
  }

  selectAll(): void {
    for (let p = 0; p < this.pages; p++) this.selected.add(p);
  }

  // ── Edits ──────────────────────────────────────────────────────────────────────────────────

  /**
   * Page `index` drawn small: from the cache when that page object was drawn already (moving a page does
   * not change it), else by the engine. A bitmap at once, or a promise while it is drawn.
   */
  thumbnail(index: number, dpi: number): PageRaster | Promise<PageRaster> | null {
    const ref = this.#refs[index];
    if (ref === undefined) return null;
    const key = `${ref}@${dpi}`;
    const cached = this.#thumbnails.get(key);
    if (cached) return cached;
    const drawing = this.engine.pageRender(index, dpi).then(
      (raster) => {
        if (this.#thumbnails.get(key) === drawing) this.#thumbnails.set(key, raster);
        return raster;
      },
      (error: unknown) => {
        this.#thumbnails.delete(key);
        throw error;
      },
    );
    this.#thumbnails.set(key, drawing);
    return drawing;
  }

  /** Rotates the selected pages by a multiple of 90 degrees, clockwise: those thumbnails are drawn again. */
  rotate(degrees: number): Promise<void> {
    const pages = this.selection;
    const rotated = new Set(pages.map((p) => this.#refs[p]));
    return this.#edit(() => this.engine.pagesRotate(pages, degrees), pages, rotated);
  }

  /** Deletes the selected pages (not all of them: a document keeps at least one page). */
  remove(): Promise<void> {
    const gone = this.selected;
    const order = this.#all().filter((p) => !gone.has(p));
    if (order.length === 0) return Promise.resolve();
    const next = Math.min(this.selection[0] ?? 0, order.length - 1);
    return this.#edit(() => this.engine.pagesArrange(order), [next]);
  }

  /** Each selected page followed by a copy of it; the copies end up selected. */
  duplicate(): Promise<void> {
    const order: number[] = [];
    const copies: number[] = [];
    for (const p of this.#all()) {
      order.push(p);
      if (this.selected.has(p)) {
        order.push(p);
        copies.push(order.length - 1);
      }
    }
    return this.#edit(() => this.engine.pagesArrange(order), copies);
  }

  /** Moves the selected pages, in their order, to stand before page `target` (= pages: at the end). */
  move(target: number): Promise<void> {
    const moving = this.selection;
    if (moving.length === 0) return Promise.resolve();
    const rest = this.#all().filter((p) => !this.selected.has(p));
    const at = rest.filter((p) => p < target).length;
    const order = [...rest.slice(0, at), ...moving, ...rest.slice(at)];
    if (order.every((p, i) => p === i)) return Promise.resolve();
    return this.#edit(
      () => this.engine.pagesArrange(order),
      moving.map((_, i) => at + i),
    );
  }

  /** The selected pages one place earlier (-1) or later (+1): the keyboard's way to reorder. */
  shift(delta: -1 | 1): Promise<void> {
    const moving = this.selection;
    if (moving.length === 0) return Promise.resolve();
    const first = moving[0]!;
    const last = moving.at(-1)!;
    if (delta < 0 && first > 0) return this.move(first - 1);
    if (delta > 0 && last < this.pages - 1) return this.move(last + 2);
    return Promise.resolve();
  }

  /** Desktop: the pages of a PDF chosen in the system's dialog (see insert). */
  async insertFromDialog(): Promise<void> {
    const picked = await pickPdf().catch((error: unknown) => {
      this.error = describe(error);
      return null;
    });
    if (picked) await this.insert(picked);
  }

  /** Every page of another PDF, after the selected pages (or at the end); the new pages end up selected. */
  async insert(source: PickedFile): Promise<void> {
    const at = this.selected.size > 0 ? this.selection.at(-1)! + 1 : this.pages;
    const before = this.pages;
    const data = source.data.slice(0);
    await this.#edit(
      () => this.engine.importFile(data, '', at),
      (summary) => Array.from({ length: (summary.pages?.length ?? before) - before }, (_, i) => at + i),
    );
  }

  /** The selected pages as a new file (downloaded, or where the save dialog says); the document stays. */
  async extract(): Promise<void> {
    const pages = this.selection;
    if (pages.length === 0 || !this.name) return;
    const name = this.name;
    await this.#run(async () => {
      const data = await this.engine.extract(pages);
      await savePdf(data, `${baseName(name)}-pagine.pdf`, null);
    });
  }

  /**
   * The document as it is now (a full save, compact): downloaded under its own name, or on the desktop
   * written over the file it came from. asNew: wherever the save dialog says (desktop).
   */
  async save(asNew = false): Promise<void> {
    if (!this.name) return;
    const name = this.name;
    const path = asNew ? null : this.path;
    await this.#run(async () => {
      const data = await this.engine.saveCompact(pdfDateNow());
      const saved = await savePdf(data, name, path);
      if (!saved) return; // cancelled
      if (saved.path !== null) {
        this.path = saved.path;
        this.name = fileName(saved.path);
      }
      this.modified = false;
    });
  }

  /** Back to the document as it was before the last edit. */
  async undo(): Promise<void> {
    const previous = this.#history.pop();
    this.canUndo = this.#history.length > 0;
    if (!previous) return;
    await this.#run(async () => {
      const opened = await this.engine.openForEditing(previous, this.#password);
      this.#show(opened.summary, []);
      this.modified = true;
      if (this.viewing !== null) {
        if (this.viewing >= this.pages) this.closeView();
        else {
          this.selectedAnnotation = null;
          await this.#loadAnnotations();
          await this.#loadFields();
        }
      }
    });
  }

  async close(): Promise<void> {
    await this.engine.close().catch(() => {});
    this.name = null;
    this.path = null;
    this.pages = 0;
    this.#refs = [];
    this.#thumbnails.clear();
    this.closeView();
    this.selected.clear();
    this.anchor = null;
    this.repairs = [];
    this.modified = false;
    this.#history = [];
    this.canUndo = false;
    this.error = null;
    this.#source = null;
  }

  // ── Annotations, on the page shown large ──────────────────────────────────────────────────────

  /** Shows page `index` large, with its annotations. */
  async view(index: number): Promise<void> {
    if (index < 0 || index >= this.pages) return;
    const first = this.viewing === null;
    this.viewing = index;
    this.selectedAnnotation = null;
    await this.#run(async () => {
      await this.#loadAnnotations();
      await this.#loadFields();
    });
    // A page with a form opens ready to fill it.
    if (
      first &&
      this.tool === 'select' &&
      this.fields.some((f) => f.fillable && f.widgets.some((w) => w.page === index))
    )
      this.tool = 'form';
  }

  closeView(): void {
    this.viewing = null;
    this.annotations = [];
    this.fields = [];
    this.selectedAnnotation = null;
    this.tool = 'select';
  }

  /** The selected annotation, as listed. */
  get annotation(): AnnotationInfo | null {
    const ref = this.selectedAnnotation;
    return ref === null ? null : (this.annotations.find((a) => a.ref?.[0] === ref[0] && a.ref?.[1] === ref[1]) ?? null);
  }

  /** Adds an annotation to the page shown; it ends up selected. */
  annotate(spec: AnnotationSpec): Promise<void> {
    const page = this.viewing;
    if (page === null) return Promise.resolve();
    return this.#editPage(async () => {
      this.selectedAnnotation = await this.engine.annotationAdd(page, { ...spec, date: pdfDateNow() });
    });
  }

  changeAnnotation(ref: [number, number], change: AnnotationChange): Promise<void> {
    const page = this.viewing;
    if (page === null) return Promise.resolve();
    return this.#editPage(() => this.engine.annotationUpdate(page, ref, { ...change, date: pdfDateNow() }));
  }

  removeAnnotation(ref: [number, number]): Promise<void> {
    const page = this.viewing;
    if (page === null) return Promise.resolve();
    return this.#editPage(async () => {
      await this.engine.annotationRemove(page, ref);
      this.selectedAnnotation = null;
    });
  }

  /** Fills a form field (a string, several for a list box, null to clear it). */
  fillField(field: FieldInfo, value: string | string[] | null): Promise<void> {
    const pages = new Set(field.widgets.map((w) => w.page).filter((p): p is number => p !== null));
    return this.#editPage(() => this.engine.formFill(field.ref, value), pages);
  }

  async #loadFields(): Promise<void> {
    this.fields = this.viewing === null ? [] : await this.engine.formFields();
  }

  async #loadAnnotations(): Promise<void> {
    this.annotations = this.viewing === null ? [] : await this.engine.annotations(this.viewing);
  }

  /**
   * An edit of the page shown (and of others: a field's widgets may be on several): kept for undo, the
   * thumbnails of those pages drawn again, the annotations and fields listed again.
   */
  async #editPage(change: () => Promise<void>, pages: Set<number> = new Set()): Promise<void> {
    // One after the other: an edit made while the last is being written (a field left for the next)
    // waits, so that each keeps for undo the document as the one before left it.
    const previous = this.#queue;
    let done = () => {};
    this.#queue = new Promise<void>((resolve) => (done = resolve));
    await previous;
    try {
      await this.#editPageNow(change, pages);
    } finally {
      done();
    }
  }

  async #editPageNow(change: () => Promise<void>, pages: Set<number>): Promise<void> {
    const page = this.viewing;
    if (page === null) return;
    await this.#run(async () => {
      const before = await this.engine.save(false, null);
      await change();
      this.#remember(before);
      for (const p of new Set([page, ...pages])) this.#forget(this.#refs[p]);
      this.version++;
      this.modified = true;
      await this.#loadAnnotations();
      await this.#loadFields();
    });
  }

  /** Forgets the thumbnails of one page object. */
  #forget(ref: string | undefined): void {
    for (const key of [...this.#thumbnails.keys()]) {
      if (key.slice(0, key.indexOf('@')) === ref) this.#thumbnails.delete(key);
    }
  }

  // ── Internals ──────────────────────────────────────────────────────────────────────────────

  #all(): number[] {
    return Array.from({ length: this.pages }, (_, i) => i);
  }

  /**
   * The document after a change: its pages, the selection, and the thumbnails to forget — those of pages
   * that changed (changed), or all of them (null: a document read anew, whose objects may be others).
   */
  #show(summary: Summary, select: number[], changed: Set<string | undefined> | null = null): void {
    this.#refs = (summary.pages ?? []).map(([number, generation]) => `${number} ${generation}`);
    const live = new Set(this.#refs);
    for (const key of [...this.#thumbnails.keys()]) {
      const ref = key.slice(0, key.indexOf('@'));
      if (changed === null || changed.has(ref) || !live.has(ref)) this.#thumbnails.delete(key);
    }
    this.pages = summary.pages?.length ?? 0;
    this.selected.clear();
    for (const p of select) if (p < this.pages) this.selected.add(p);
    this.anchor = select[0] ?? null;
    this.version++;
  }

  /** An edit: the document is kept for undo first, then changed; select picks the pages to select after. */
  async #edit(
    change: () => Promise<Summary>,
    select: number[] | ((summary: Summary) => number[]),
    changed: Set<string | undefined> = new Set(),
  ): Promise<void> {
    await this.#run(async () => {
      const before = await this.engine.save(false, null);
      const summary = await change();
      this.#remember(before);
      this.#show(summary, typeof select === 'function' ? select(summary) : select, changed);
      this.modified = true;
    });
  }

  #remember(data: ArrayBuffer): void {
    this.#history.push(data);
    let total = this.#history.reduce((sum, d) => sum + d.byteLength, 0);
    while (this.#history.length > 1 && total > kHistoryBytes) total -= this.#history.shift()!.byteLength;
    this.canUndo = true;
  }

  async #run(task: () => Promise<void>): Promise<void> {
    this.busy = true;
    this.error = null;
    try {
      await task();
    } catch (error) {
      this.error = describe(error);
    } finally {
      this.busy = false;
    }
  }
}

function describe(error: unknown): string {
  if (error instanceof EngineError) {
    if (error.status === Status.passwordRequired) return 'Il file è protetto da password.';
    return error.message;
  }
  return error instanceof Error ? error.message : String(error);
}

export const [getEditor, setEditor] = createContext<Editor>();
