// The page editor's state: the open file, its pages, the selection, and the edits — each a call to the
// engine (packages/engine-client), which changes the document in its worker; nothing leaves the browser.
import { createContext } from 'svelte';
import { SvelteSet } from 'svelte/reactivity';
import { EngineClient, EngineError, Status, type Repair, type Summary } from '@capriole/engine-client';
import { baseName, download, pdfDateNow } from './files';

/** Undo keeps full copies of the document before each edit, within this many bytes (oldest dropped). */
const kHistoryBytes = 256 * 1024 * 1024;

export class Editor {
  readonly engine = new EngineClient();

  engineVersion = $state<string | null>(null);
  /** The open file's name; null when no file is open. */
  name = $state<string | null>(null);
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

  #file: File | null = null;
  #password = '';
  #history: ArrayBuffer[] = [];

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

  async open(file: File, password = ''): Promise<void> {
    this.#file = file;
    this.#password = password;
    await this.#run(async () => {
      try {
        const opened = await this.engine.openForEditing(await file.arrayBuffer(), password);
        this.name = file.name;
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
    if (this.#file) await this.open(this.#file, password);
  }

  cancelPassword(): void {
    this.passwordNeeded = false;
    this.passwordRejected = false;
    this.#file = null;
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

  /** Rotates the selected pages by a multiple of 90 degrees, clockwise. */
  rotate(degrees: number): Promise<void> {
    const pages = this.selection;
    return this.#edit(() => this.engine.pagesRotate(pages, degrees), pages);
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

  /** Every page of another PDF, after the selected pages (or at the end); the new pages end up selected. */
  async insert(file: File): Promise<void> {
    const at = this.selected.size > 0 ? this.selection.at(-1)! + 1 : this.pages;
    const before = this.pages;
    const data = await file.arrayBuffer();
    await this.#edit(
      () => this.engine.importFile(data, '', at),
      (summary) => Array.from({ length: (summary.pages?.length ?? before) - before }, (_, i) => at + i),
    );
  }

  /** The selected pages as a new file, downloaded; the document stays as it is. */
  async extract(): Promise<void> {
    const pages = this.selection;
    if (pages.length === 0 || !this.name) return;
    const name = this.name;
    await this.#run(async () => {
      const data = await this.engine.extract(pages);
      download(data, `${baseName(name)}-pagine.pdf`);
    });
  }

  /** The document as it is now, downloaded under its own name (a full save, compact). */
  async save(): Promise<void> {
    if (!this.name) return;
    const name = this.name;
    await this.#run(async () => {
      const data = await this.engine.saveCompact(pdfDateNow());
      download(data, name);
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
    });
  }

  async close(): Promise<void> {
    await this.engine.close().catch(() => {});
    this.name = null;
    this.pages = 0;
    this.selected.clear();
    this.anchor = null;
    this.repairs = [];
    this.modified = false;
    this.#history = [];
    this.canUndo = false;
    this.error = null;
    this.#file = null;
  }

  // ── Internals ──────────────────────────────────────────────────────────────────────────────

  #all(): number[] {
    return Array.from({ length: this.pages }, (_, i) => i);
  }

  #show(summary: Summary, select: number[]): void {
    this.pages = summary.pages?.length ?? 0;
    this.selected.clear();
    for (const p of select) if (p < this.pages) this.selected.add(p);
    this.anchor = select[0] ?? null;
    this.version++;
  }

  /** An edit: the document is kept for undo first, then changed; select picks the pages to select after. */
  async #edit(change: () => Promise<Summary>, select: number[] | ((summary: Summary) => number[])): Promise<void> {
    await this.#run(async () => {
      const before = await this.engine.save(false, null);
      const summary = await change();
      this.#remember(before);
      this.#show(summary, typeof select === 'function' ? select(summary) : select);
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
