// The page's side of the engine worker: one promise per request.
import type {
  AnnotationChange,
  AnnotationInfo,
  AnnotationSpec,
  AttachmentInfo,
  AttachmentSpec,
  FacturxInfo,
  FacturxResult,
  FieldInfo,
  RedactionReport,
  TextEditReport,
  ParagraphEditReport,
  ParagraphRoom,
  CertificateInfo,
  FontFace,
  SignatureInfo,
  SignSpec,
  PdfaReport,
  ObjectInfo,
  PageContent,
  PagePreview,
  PageRaster,
  PageText,
  EditableDocument,
  Request,
  Response,
  Summary,
  TextOrder,
} from './types';

export class EngineError extends Error {
  constructor(
    readonly status: number,
    message: string,
  ) {
    super(message);
    this.name = 'EngineError';
  }
}

export class EngineClient {
  #worker = new Worker(new URL('./worker.ts', import.meta.url), { type: 'module' });
  #next = 1;
  #pending = new Map<number, { resolve: (value: unknown) => void; reject: (error: Error) => void }>();

  constructor() {
    this.#worker.onmessage = (event: MessageEvent<Response>) => {
      const response = event.data;
      const pending = this.#pending.get(response.id);
      if (!pending) return;
      this.#pending.delete(response.id);
      if (response.ok) pending.resolve(response.value);
      else pending.reject(new EngineError(response.status, response.message));
    };
    this.#worker.onerror = (event) => {
      // An error event without a message: the worker's script did not load at all (in development, a
      // server started before its configuration changed serves an old address — restart it).
      const reason =
        event.message || 'its worker could not start (the script did not load; in development, restart the server)';
      const error = new EngineError(101, `the engine stopped: ${reason}`);
      for (const pending of this.#pending.values()) pending.reject(error);
      this.#pending.clear();
    };
    // Where the resource pack is served: fonts and CMaps a page needs are fetched from there.
    // A failure here (the worker did not start) is reported by the requests that follow.
    this.#call({ kind: 'resourceBase', base: new URL('resources/', document.baseURI).href }).catch(() => {});
  }

  #call<T>(request: Request, transfer: Transferable[] = []): Promise<T> {
    const id = this.#next++;
    return new Promise<T>((resolve, reject) => {
      this.#pending.set(id, { resolve: resolve as (value: unknown) => void, reject });
      this.#worker.postMessage({ id, request }, transfer);
    });
  }

  version(): Promise<string> {
    return this.#call({ kind: 'version' });
  }

  /** The bytes are handed to the worker (the buffer is no longer usable here). */
  open(data: ArrayBuffer, password: string, repair: boolean): Promise<Summary> {
    return this.#call({ kind: 'open', data, password, repair }, [data]);
  }

  object(number: number): Promise<ObjectInfo> {
    return this.#call({ kind: 'object', number });
  }

  /** The operations of page `index` (0-based), at most `max` of them. */
  pageContent(index: number, max: number): Promise<PageContent> {
    return this.#call({ kind: 'pageContent', index, max });
  }

  /** Page `index` (0-based) interpreted and drawn as SVG. */
  pagePreview(index: number): Promise<PagePreview> {
    return this.#call({ kind: 'pagePreview', index });
  }

  /** Page `index` (0-based) drawn by the engine's rasterizer at `dpi` dots per inch. */
  pageRender(index: number, dpi: number): Promise<PageRaster> {
    return this.#call({ kind: 'pageRender', index, dpi });
  }

  /** The text of page `index` (0-based): words, lines, blocks and every character's provenance. */
  pageText(index: number, order: TextOrder): Promise<PageText> {
    return this.#call({ kind: 'pageText', index, order });
  }

  /**
   * Opens a file to edit its pages. A file that needed repairs is saved fully and read back first (page
   * operations work on a sound document, edit/pages.hpp): the repairs come back for the user to see.
   */
  openForEditing(data: ArrayBuffer, password: string): Promise<EditableDocument> {
    return this.#call({ kind: 'openForEditing', data, password }, [data]);
  }

  /** Rotates pages (indices from 0) by a multiple of 90 degrees, clockwise. */
  pagesRotate(pages: number[], degrees: number): Promise<Summary> {
    return this.#call({ kind: 'pagesRotate', pages, degrees });
  }

  /** The pages, in this order, by their current indices: left out = deleted, repeated = duplicated. */
  pagesArrange(order: number[]): Promise<Summary> {
    return this.#call({ kind: 'pagesArrange', order });
  }

  /** Every page of another file, inserted before page `at` (= the page count: appended). */
  importFile(data: ArrayBuffer, password: string, at: number): Promise<Summary> {
    return this.#call({ kind: 'importFile', data, password, at }, [data]);
  }

  /** A new file with these pages only; the open document stays as it is. */
  extract(pages: number[]): Promise<ArrayBuffer> {
    return this.#call({ kind: 'extract', pages });
  }

  /** A full save without the objects nothing uses any more. */
  saveCompact(modificationDate: string | null): Promise<ArrayBuffer> {
    return this.#call({ kind: 'saveCompact', modificationDate });
  }

  /** The annotations of page `index`. */
  annotations(index: number): Promise<AnnotationInfo[]> {
    return this.#call({ kind: 'annotations', index });
  }

  /** Adds an annotation to page `index` (its appearance made by the engine); resolves to its reference. */
  annotationAdd(index: number, spec: AnnotationSpec): Promise<[number, number]> {
    return this.#call({ kind: 'annotationAdd', index, spec });
  }

  annotationUpdate(index: number, ref: [number, number], change: AnnotationChange): Promise<void> {
    return this.#call({ kind: 'annotationUpdate', index, ref, change });
  }

  annotationRemove(index: number, ref: [number, number]): Promise<void> {
    return this.#call({ kind: 'annotationRemove', index, ref });
  }

  /** The document's attachments (its own, then those of annotations). */
  attachments(): Promise<AttachmentInfo[]> {
    return this.#call({ kind: 'attachments' });
  }

  /** An attachment's bytes, and whether they match its checksum (null: it has none). */
  attachmentData(ref: [number, number]): Promise<{ data: ArrayBuffer; checksum: boolean | null }> {
    return this.#call({ kind: 'attachmentData', ref });
  }

  /** Attaches a file to the document (associated with it); resolves to its file specification. The bytes are transferred. */
  attachmentAdd(spec: AttachmentSpec, data: ArrayBuffer): Promise<[number, number]> {
    return this.#call({ kind: 'attachmentAdd', spec, data }, [data]);
  }

  attachmentRemove(ref: [number, number]): Promise<void> {
    return this.#call({ kind: 'attachmentRemove', ref });
  }

  /** The document's Factur-X invoice; null when it has none. */
  facturx(): Promise<FacturxInfo | null> {
    return this.#call({ kind: 'facturx' });
  }

  /** Embeds an invoice's XML as the document's Factur-X invoice (date: a PDF date). The bytes are transferred. */
  facturxEmbed(xml: ArrayBuffer, date: string): Promise<FacturxResult> {
    return this.#call({ kind: 'facturxEmbed', xml, date }, [xml]);
  }

  /**
   * Applies the redaction marks of page index: what lies in them is taken out of the file. The document
   * is then saved fully only (saveCompact, save with incremental false).
   */
  applyRedactions(index: number): Promise<RedactionReport> {
    return this.#call({ kind: 'applyRedactions', index });
  }

  /**
   * Replaces the text of the characters in area (the page as shown, one line) with text (empty: deleted):
   * in the same font when it can, else in a similar standard font. Refused (Status.unsupported) for
   * several lines, invisible text, an annotation's text; Status.outOfRange when there is no text there.
   */
  replaceText(index: number, area: [number, number, number, number], text: string): Promise<TextEditReport> {
    return this.#call({ kind: 'replaceText', index, area, text });
  }

  /**
   * Replaces a paragraph's text — the lines area touches (the page as shown), whole — laid out anew inside
   * it ("\n": a new line; empty: deleted). Refused (Status.unsupported) when it does not fit ("no room: …
   * needs N lines, M fit") unless room says what to do then — written smaller, or what follows moved
   * down (refused, "cannot move what follows: …", when that cannot be done) —, and for paragraphs of
   * several styles, invisible or an annotation's text; Status.outOfRange when there is no text there.
   */
  replaceParagraph(
    index: number,
    area: [number, number, number, number],
    text: string,
    room: ParagraphRoom = 'none',
  ): Promise<ParagraphEditReport> {
    return this.#call({ kind: 'replaceParagraph', index, area, text, room });
  }

  /**
   * Gives the engine a full font (.ttf, .otf with TrueType outlines, .ttc): when an edit of text needs a
   * letter an embedded subset of the same font lacks, its glyph comes from there (if the font's
   * permissions allow it). Shared by every document, until fontsClear. Resolves to its faces; refused
   * (Status.unsupported, Status.malformed) for a file that is not such a font. The bytes are transferred.
   */
  fontAdd(data: ArrayBuffer): Promise<FontFace[]> {
    return this.#call({ kind: 'fontAdd', data }, [data]);
  }

  /** The faces of the fonts given. */
  fonts(): Promise<FontFace[]> {
    return this.#call({ kind: 'fonts' });
  }

  /** Forgets the fonts given. */
  fontsClear(): Promise<void> {
    return this.#call({ kind: 'fontsClear' });
  }

  /** The certificate of a PKCS#12 file (.p12, .pfx); Status.passwordRequired for a wrong password. The bytes are transferred. */
  pkcs12Info(p12: ArrayBuffer, password: string): Promise<CertificateInfo> {
    return this.#call({ kind: 'pkcs12Info', p12, password }, [p12]);
  }

  /**
   * Signs the document with the key of a PKCS#12 file (no time stamp): the signature is a new revision.
   * Resolves to the signed file, which the document now is (what is edited next is appended after the
   * signature: save it incrementally). The bytes of the key file are transferred.
   */
  sign(spec: SignSpec, p12: ArrayBuffer, password: string): Promise<ArrayBuffer> {
    return this.#call({ kind: 'sign', spec, p12, password }, [p12]);
  }

  /**
   * Converts the document to PDF/A (part 2 or 3; 0: 2, or 3 with attachments; date: a PDF date): what was
   * put right. The document is then the converted file. A document that cannot be made PDF/A is refused
   * (Status.unsupported), every reason in the message.
   */
  convertPdfa(part: number, date: string): Promise<PdfaReport> {
    return this.#call({ kind: 'convertPdfa', part, date });
  }

  /** The open document's summary (its pages, after it was read anew). */
  summary(): Promise<Summary> {
    return this.#call({ kind: 'summary' });
  }

  /** The document's signatures, verified (the signers' identity is not checked: no trusted roots). */
  signatures(): Promise<SignatureInfo[]> {
    return this.#call({ kind: 'signatures' });
  }

  /** The fields of the document's form (none without one). */
  formFields(): Promise<FieldInfo[]> {
    return this.#call({ kind: 'formFields' });
  }

  /** Fills a field: a string, several (list boxes), or null to clear it; its widgets get the value's appearance. */
  formFill(ref: [number, number], value: string | string[] | null): Promise<void> {
    return this.#call({ kind: 'formFill', ref, value });
  }

  stream(number: number, decoded: boolean): Promise<ArrayBuffer> {
    return this.#call({ kind: 'stream', number, decoded });
  }

  save(incremental: boolean, modificationDate: string | null): Promise<ArrayBuffer> {
    return this.#call({ kind: 'save', incremental, modificationDate });
  }

  close(): Promise<void> {
    return this.#call({ kind: 'close' });
  }
}
