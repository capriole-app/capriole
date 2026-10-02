// The page's side of the engine worker: one promise per request.
import type {
  AnnotationChange,
  AnnotationInfo,
  AnnotationSpec,
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
