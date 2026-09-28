// The page's side of the engine worker: one promise per request.
import type { ObjectInfo, PageContent, PagePreview, PageRaster, Request, Response, Summary } from './types';

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
      const error = new EngineError(101, `the engine stopped: ${event.message}`);
      for (const pending of this.#pending.values()) pending.reject(error);
      this.#pending.clear();
    };
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
