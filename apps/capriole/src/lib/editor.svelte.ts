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
  type AttachmentInfo,
  type FacturxInfo,
  type FieldInfo,
  type PageRaster,
  type PageText,
  type PdfaReport,
  type Repair,
  type SignatureInfo,
  type Summary,
} from '@capriole/engine-client';
import { baseName, dateTime, pdfDateNow } from './files';
import { parseInvoice, type Invoice } from './invoice';
import { lineText, replacementFor, textEditNotice, textEditRefusal, type LineText } from './textedit';
import { fromBrowser, pickPdf, saveFile, savePdf, type PickedFile } from './platform';

/**
 * What a pointer does on the page shown large: fill the form, select (and move) annotations, make one, or
 * edit the page's own text (edittext).
 */
export type Tool = 'form' | 'select' | 'sign' | 'edittext' | AnnotationSpec['kind'];

/** A line of the page's text being written again, and the page's text it was read from. */
export interface LineEditing {
  line: LineText;
  page: PageText;
}

/** A signature to make: on page (from 0), visible in rect (the page as shown), or invisible (null). */
export interface SignRequest {
  page: number;
  rect: [number, number, number, number] | null;
}

/** What the person writes in the signature dialog. */
export interface SignDetails {
  reason: string;
  location: string;
}

export type Rgb = [number, number, number];

/**
 * Free text being written on the page shown: a new one, or one of its own (ref). grow: the box widens as
 * the text is typed (made by a click); wrap: its width is fixed, lines wrap and it grows downwards
 * (made by dragging a rectangle, or an existing one). rect: the page as shown, points.
 */
export interface Typing {
  ref: [number, number] | null;
  rect: [number, number, number, number];
  mode: 'grow' | 'wrap';
  text: string;
  fontSize: number;
  color: Rgb;
}

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
  /** The document's attachments, its Factur-X invoice (null: none) and what its XML says. */
  attachments = $state.raw<AttachmentInfo[]>([]);
  facturx = $state.raw<FacturxInfo | null>(null);
  invoice = $state.raw<Invoice | null>(null);
  /** The attachments' panel, beside the pages. */
  attachmentsOpen = $state(false);
  /** The document's signatures, verified; their panel beside the pages. */
  signatures = $state.raw<SignatureInfo[]>([]);
  signaturesOpen = $state(false);
  /** The signature being made (the dialog is open); null: none. */
  signRequest = $state.raw<SignRequest | null>(null);
  /** The PDF/A part the document declares (null: none): it is then saved by appending, like a signed one. */
  pdfa = $state<number | null>(null);
  /** The PDF/A dialog: the part asked for (0: 2, or 3 with attachments) while it is open, null when closed. */
  pdfaDialog = $state<number | null>(null);
  /** What the last conversion put right, or why it was refused (the engine's reasons). */
  pdfaReport = $state.raw<PdfaReport | null>(null);
  pdfaRefusal = $state.raw<string[] | null>(null);
  /** The «Informazioni e licenze» dialog. */
  licensesOpen = $state(false);
  /** The size of the next free text (points). */
  fontSize = $state(12);
  /** The free text being written on the page (null: none). */
  typing = $state.raw<Typing | null>(null);
  /** The line of the page's text being written again (null: none). */
  editingLine = $state.raw<LineEditing | null>(null);
  /** How the last edit of the page's text was written, when worth saying (a similar font). */
  textNotice = $state<string | null>(null);
  /** An existing free text being written again: the document before (for undo) and its text then. */
  #retyping: { before: ArrayBuffer; text: string; rect: Typing['rect'] } | null = null;

  #source: PickedFile | null = null;
  /** Browser with file access: the file the document is saved to ("Save" writes it again). */
  #handle: FileSystemFileHandle | null = null;
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

  /** A file chosen in the system's dialog (desktop, or a browser with file access). */
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
        this.#handle = source.handle ?? null;
        this.repairs = opened.repairs;
        this.#history = [];
        this.canUndo = false;
        this.passwordNeeded = false;
        this.passwordRejected = false;
        this.#show(opened.summary, []);
        this.modified = false;
        await this.#loadAttachments();
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
      await savePdf(() => this.engine.extract(pages), `${baseName(name)}-pagine.pdf`, null);
    });
  }

  /**
   * The document as it is now (a full save, compact), written over the file it came from or was last
   * saved to (desktop, or a browser with file access; otherwise where the save dialog says), or
   * downloaded under its own name. asNew: wherever the save dialog says.
   */
  async save(asNew = false): Promise<void> {
    if (!this.name) return;
    // Not awaited before the dialog (the browser shows it only right after the click): the bytes are
    // made after the edits being written.
    const pending = this.#queue;
    const name = this.name;
    const target = asNew ? null : { path: this.path, handle: this.#handle };
    await this.#run(async () => {
      const saved = await savePdf(
        async () => {
          await pending;
          return this.#bytes();
        },
        name,
        target,
      );
      if (!saved) return; // cancelled
      if (saved.path !== null || saved.handle !== null) {
        this.path = saved.path;
        this.#handle = saved.handle;
        this.name = saved.name;
      }
      this.modified = false;
    });
  }

  // ── PDF/A ──────────────────────────────────────────────────────────────────────────────────

  /** Opens the PDF/A dialog (part: 3 for an invoice's PDF/A-3; 0: as the document needs). */
  openPdfa(part = 0): void {
    this.pdfaReport = null;
    this.pdfaRefusal = null;
    this.pdfaDialog = part;
  }

  closePdfa(): void {
    this.pdfaDialog = null;
    this.pdfaReport = null;
    this.pdfaRefusal = null;
  }

  /**
   * Converts the document to PDF/A (one step of undo): what was put right goes to pdfaReport; a refusal
   * (the document unchanged) to pdfaRefusal, its reasons one each.
   */
  convertToPdfa(): Promise<void> {
    const part = this.pdfaDialog ?? 0;
    return this.#serial(() =>
      this.#run(async () => {
        this.pdfaReport = null;
        this.pdfaRefusal = null;
        const before = await this.#snapshot();
        try {
          this.pdfaReport = await this.engine.convertPdfa(part, pdfDateNow());
        } catch (error) {
          if (error instanceof EngineError && error.status === Status.unsupported) {
            const text = error.message.replace(/^.*?cannot be made PDF\/A: /, '');
            this.pdfaRefusal = text.split('; ');
            return;
          }
          throw error;
        }
        this.#remember(before);
        this.#show(await this.engine.summary(), this.selection);
        this.modified = true;
        await this.#loadAttachments();
        if (this.viewing !== null) {
          await this.#loadAnnotations();
          await this.#loadFields();
        }
      }),
    );
  }

  /** Saves the document converted to PDF/A as a new file ("…-pdfa.pdf"): where the save dialog says, or downloaded. */
  async savePdfa(): Promise<void> {
    if (!this.name) return;
    const name = this.name;
    const pending = this.#queue;
    await this.#run(async () => {
      const saved = await savePdf(
        async () => {
          await pending;
          return this.#bytes();
        },
        `${baseName(name).replace(/-pdfa$/, '')}-pdfa.pdf`,
        null,
      );
      if (!saved) return;
      if (saved.path !== null || saved.handle !== null) {
        this.path = saved.path;
        this.#handle = saved.handle;
      }
      this.name = saved.name;
      this.modified = false;
      this.closePdfa();
    });
  }

  /** Whether the document carries signatures: it is then saved by appending, never written anew. */
  get signed(): boolean {
    return this.signatures.length > 0;
  }

  /**
   * The document's bytes to save. Signed: appended to the file as it was (a full save would change the
   * bytes the signatures cover; without changes, the file itself); else a full save, compact. A signed
   * document that cannot be appended to (redacted) is written anew: its signatures no longer hold.
   */
  async #bytes(): Promise<ArrayBuffer> {
    if (this.signed || this.pdfa !== null) {
      try {
        return await this.engine.save(true, null);
      } catch {
        // redacted: written anew below
      }
    }
    return this.engine.saveCompact(pdfDateNow());
  }

  /** The document before an edit, for undo: appended when it is signed (see #bytes), else in full. */
  async #snapshot(): Promise<ArrayBuffer> {
    if (this.signed || this.pdfa !== null) {
      try {
        return await this.engine.save(true, null);
      } catch {
        // redacted
      }
    }
    return this.engine.save(false, null);
  }

  // ── Signatures ─────────────────────────────────────────────────────────────────────────────

  /** Opens the signature dialog: visible in rect on page, or invisible (rect null). */
  requestSignature(page: number, rect: SignRequest['rect']): void {
    this.signRequest = { page, rect };
  }

  /**
   * Signs the document with the key of a PKCS#12 file and saves the signed file — where the save
   * dialog says (named "…-firmato.pdf"), or downloaded: the dialog comes first (the browser shows it only
   * right after the click), the signature when it is answered; cancelled, nothing is signed. The changes
   * not saved yet are part of the signed revision. One step of undo.
   */
  async sign(p12: ArrayBuffer, password: string, details: SignDetails): Promise<boolean> {
    const request = this.signRequest;
    if (!request || !this.name) return false;
    const pending = this.#queue;
    const lines = ['Firmato digitalmente da {signer}', `Data: ${dateTime(new Date())}`];
    if (details.reason) lines.push(`Motivo: ${details.reason}`);
    if (details.location) lines.push(`Luogo: ${details.location}`);
    let signedAt: Summary | null = null;
    let done = false;
    await this.#run(async () => {
      const saved = await savePdf(
        async () => {
          await pending;
          const before = await this.#snapshot();
          const data = await this.engine.sign(
            {
              date: pdfDateNow(),
              page: request.page,
              rect: request.rect,
              lines,
              reason: details.reason || undefined,
              location: details.location || undefined,
            },
            p12,
            password,
          );
          this.#remember(before);
          signedAt = await this.engine.summary();
          return data;
        },
        `${baseName(this.name!)}-firmato.pdf`,
        null,
      );
      if (!saved) return; // cancelled: nothing signed
      if (saved.path !== null || saved.handle !== null) {
        this.path = saved.path;
        this.#handle = saved.handle;
      }
      this.name = saved.name;
      this.modified = false;
      this.signRequest = null;
      done = true;
      // The signed file was read anew: its objects may be others.
      if (signedAt) this.#show(signedAt, this.selection);
      await this.#loadAttachments();
      if (this.viewing !== null) {
        await this.#loadAnnotations();
        await this.#loadFields();
      }
    });
    return done;
  }

  /** Back to the document as it was before the last edit. */
  async undo(): Promise<void> {
    await this.#queue; // an edit being written (a text box just left) first
    const previous = this.#history.pop();
    this.canUndo = this.#history.length > 0;
    if (!previous) return;
    await this.#run(async () => {
      const opened = await this.engine.openForEditing(previous, this.#password);
      this.#show(opened.summary, []);
      this.modified = true;
      await this.#loadAttachments();
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

  // ── Attachments and Factur-X ────────────────────────────────────────────────────────────────

  /** Attaches files to the document (associated with it); one step of undo for them all. */
  addAttachments(files: File[]): Promise<void> {
    if (files.length === 0) return Promise.resolve();
    return this.#editDocument(async () => {
      for (const file of files) {
        const date = pdfDateNow();
        await this.engine.attachmentAdd(
          {
            fileName: file.name,
            mimeType: file.type,
            relationship: 'Supplement',
            creationDate: date,
            modificationDate: date,
          },
          await file.arrayBuffer(),
        );
      }
    });
  }

  removeAttachment(ref: [number, number]): Promise<void> {
    return this.#editDocument(() => this.engine.attachmentRemove(ref));
  }

  /** An attachment saved to disk (where the save dialog says), or downloaded. */
  async saveAttachment(a: AttachmentInfo): Promise<void> {
    await this.#run(async () => {
      await saveFile(
        async () => (await this.engine.attachmentData(a.ref)).data,
        a.fileName || a.name || 'allegato',
        null,
        a.mimeType || 'application/octet-stream',
      );
    });
  }

  /** Makes the document a Factur-X invoice with the XML of one (a Cross Industry Invoice). */
  embedInvoice(xml: File): Promise<void> {
    return this.#editDocument(async () => {
      await this.engine.facturxEmbed(await xml.arrayBuffer(), pdfDateNow());
    });
  }

  async #loadAttachments(): Promise<void> {
    this.signatures = await this.engine.signatures();
    this.attachments = await this.engine.attachments();
    this.facturx = await this.engine.facturx();
    this.invoice = null;
    if (this.facturx) {
      const { data } = await this.engine.attachmentData(this.facturx.ref);
      this.invoice = parseInvoice(new TextDecoder().decode(data));
    }
  }

  /** An edit of the document as a whole (not of a page): kept for undo, the attachments listed again. */
  #editDocument(change: () => Promise<void>): Promise<void> {
    return this.#serial(() =>
      this.#run(async () => {
        const before = await this.#snapshot();
        await change();
        this.#remember(before);
        this.modified = true;
        await this.#loadAttachments();
      }),
    );
  }

  async close(): Promise<void> {
    await this.engine.close().catch(() => {});
    this.name = null;
    this.path = null;
    this.#handle = null;
    this.pages = 0;
    this.attachments = [];
    this.facturx = null;
    this.invoice = null;
    this.attachmentsOpen = false;
    this.signatures = [];
    this.signaturesOpen = false;
    this.signRequest = null;
    this.pdfa = null;
    this.closePdfa();
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
    this.typing = null;
    this.#retyping = null;
    this.editingLine = null;
    this.textNotice = null;
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

  /** Starts writing a new free text on the page shown: a box at rect (grow: widening as typed). */
  startTyping(rect: Typing['rect'], mode: Typing['mode']): void {
    if (this.viewing === null || this.typing) return;
    this.selectedAnnotation = null;
    this.typing = { ref: null, rect, mode, text: '', fontSize: this.fontSize, color: [...this.color] as Rgb };
  }

  /**
   * Writes again a free text of the page shown, in place: its drawn text is taken away while it is
   * written (the box shows it), and the whole writing is one step of undo.
   */
  async retype(ref: [number, number]): Promise<void> {
    const a = this.annotations.find((x) => x.ref?.[0] === ref[0] && x.ref?.[1] === ref[1]);
    const page = this.viewing;
    if (!a || a.subtype !== 'FreeText' || page === null || this.typing) return;
    await this.#serial(() =>
      this.#run(async () => {
        const before = await this.#snapshot();
        await this.engine.annotationUpdate(page, ref, { contents: '' });
        this.#retyping = { before, text: a.contents, rect: a.rect };
        this.selectedAnnotation = ref;
        this.typing = {
          ref,
          rect: a.rect,
          mode: 'wrap',
          text: a.contents,
          fontSize: a.fontSize ?? 12,
          color: (a.color ?? [0, 0, 0]) as Rgb,
        };
        this.#forget(this.#refs[page]);
        this.version++;
      }),
    );
  }

  /**
   * Ends the writing: the text in its box (rect, as measured on the page) becomes the free text — a new
   * one, or the one written again; an empty text makes none (or removes it).
   */
  async finishTyping(text: string, rect: Typing['rect']): Promise<void> {
    const typing = this.typing;
    const page = this.viewing;
    this.typing = null;
    if (!typing || page === null) return;
    const empty = text.trim() === '';
    if (typing.ref === null) {
      if (!empty) {
        await this.annotate({ kind: 'freetext', rect, contents: text, fontSize: typing.fontSize, color: typing.color });
      }
      return;
    }
    const ref = typing.ref;
    const retyping = this.#retyping;
    this.#retyping = null;
    if (!retyping) return;
    const same = text === retyping.text && rect.every((v, i) => Math.abs(v - retyping.rect[i]!) < 0.5);
    await this.#serial(() =>
      this.#run(async () => {
        if (same) {
          // Nothing changed: the text back as it was, and no step of undo.
          await this.engine.annotationUpdate(page, ref, { contents: retyping.text });
        } else {
          if (empty) {
            await this.engine.annotationRemove(page, ref);
            this.selectedAnnotation = null;
          } else {
            await this.engine.annotationUpdate(page, ref, { contents: text, rect, date: pdfDateNow() });
          }
          this.#remember(retyping.before);
          this.modified = true;
        }
        this.#forget(this.#refs[page]);
        this.version++;
        await this.#loadAnnotations();
      }),
    );
  }

  // ── The page's own text ───────────────────────────────────────────────────────────────────────

  /** Starts writing again line `line` of the page shown (page: its text, as read now). */
  startEditingLine(page: PageText, line: number): void {
    if (this.viewing === null || this.typing || this.editingLine) return;
    const read = lineText(page, line);
    this.textNotice = null;
    if (read.invisible) {
      this.error = textEditRefusal('invisible');
      return;
    }
    this.error = null;
    this.editingLine = { line: read, page };
  }

  /**
   * Ends writing the line: text replaces what changed in it (one step of undo; nothing changed: none);
   * null leaves it as it was. How it was written, when it took a similar font, goes to textNotice; a
   * refusal, in Italian, to error.
   */
  async finishEditingLine(text: string | null): Promise<void> {
    const editing = this.editingLine;
    const pageIndex = this.viewing;
    this.editingLine = null;
    if (!editing || pageIndex === null || text === null) return;
    const replacement = replacementFor(editing.page, editing.line.index, text);
    if (!replacement) return;
    await this.#editPage(async () => {
      try {
        const report = await this.engine.replaceText(pageIndex, replacement.area, replacement.text);
        this.textNotice = textEditNotice(report);
      } catch (error) {
        if (error instanceof EngineError && (error.status === Status.unsupported || error.status === Status.outOfRange))
          throw new Error(textEditRefusal(error.message));
        throw error;
      }
    });
  }

  /** The redaction marks of the page shown. */
  get redactionMarks(): AnnotationInfo[] {
    return this.annotations.filter((a) => a.subtype === 'Redact');
  }

  /** Applies the page's redaction marks: what lies in them leaves the file (one step of undo, until saved). */
  applyRedactions(): Promise<void> {
    const page = this.viewing;
    if (page === null) return Promise.resolve();
    return this.#editPage(async () => {
      await this.engine.applyRedactions(page);
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
    await this.#serial(() => this.#editPageNow(change, pages));
  }

  /**
   * One after the other: an edit made while the last is being written (a field left for the next, a
   * text box left by clicking "Annulla") waits, so that each keeps for undo the document as the one
   * before left it.
   */
  async #serial(task: () => Promise<void>): Promise<void> {
    const previous = this.#queue;
    let done = () => {};
    this.#queue = new Promise<void>((resolve) => (done = resolve));
    await previous;
    try {
      await task();
    } finally {
      done();
    }
  }

  async #editPageNow(change: () => Promise<void>, pages: Set<number>): Promise<void> {
    const page = this.viewing;
    if (page === null) return;
    await this.#run(async () => {
      const before = await this.#snapshot();
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
    this.pdfa = summary.pdfa ?? null;
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
      const before = await this.#snapshot();
      const summary = await change();
      this.#remember(before);
      this.#show(summary, typeof select === 'function' ? select(summary) : select, changed);
      this.modified = true;
      await this.#loadAttachments();
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
