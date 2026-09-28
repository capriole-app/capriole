// The engine in a worker: loads the WebAssembly module and answers the page's requests through the
// C API. One document open at a time.
import createCapriole, { type CaprioleModule } from './generated/capriole.mjs';
import { Status, type Request, type Response } from './types';

class EngineFailure extends Error {
  constructor(
    readonly status: number,
    message: string,
  ) {
    super(message);
  }
}

const engine: Promise<CaprioleModule> = createCapriole();
let documentHandle = 0;

function check(m: CaprioleModule, status: number): void {
  if (status !== Status.ok) {
    throw new EngineFailure(status, m.UTF8ToString(m._capriole_last_error_message()));
  }
}

/** Runs fn with `size` bytes of scratch memory in the module. */
function withScratch<T>(m: CaprioleModule, size: number, fn: (pointer: number) => T): T {
  const pointer = m._malloc(size);
  try {
    return fn(pointer);
  } finally {
    m._free(pointer);
  }
}

function withString<T>(m: CaprioleModule, text: string, fn: (pointer: number) => T): T {
  const size = m.lengthBytesUTF8(text) + 1;
  return withScratch(m, size, (pointer) => {
    m.stringToUTF8(text, pointer, size);
    return fn(pointer);
  });
}

/** A JSON result: out receives a char* the module allocated. */
function takeJson(m: CaprioleModule, call: (out: number) => number): unknown {
  return withScratch(m, 4, (out) => {
    check(m, call(out));
    const pointer = m.HEAPU32[out >> 2]!;
    try {
      return JSON.parse(m.UTF8ToString(pointer));
    } finally {
      m._capriole_free(pointer);
    }
  });
}

/** A byte result: outData receives a uint8_t*, outSize its size. Copied out of the module's memory. */
function takeBytes(m: CaprioleModule, call: (outData: number, outSize: number) => number): ArrayBuffer {
  return withScratch(m, 8, (out) => {
    check(m, call(out, out + 4));
    const pointer = m.HEAPU32[out >> 2]!;
    const size = m.HEAPU32[(out >> 2) + 1]!;
    try {
      return m.HEAPU8.slice(pointer, pointer + size).buffer;
    } finally {
      m._capriole_free(pointer);
    }
  });
}

function requireDocument(): number {
  if (documentHandle === 0) {
    throw new EngineFailure(Status.invalidArgument, 'no document is open');
  }
  return documentHandle;
}

async function handle(request: Request): Promise<{ value: unknown; transfer: Transferable[] }> {
  const m = await engine;
  switch (request.kind) {
    case 'version':
      return { value: m.UTF8ToString(m._capriole_version()), transfer: [] };
    case 'open': {
      if (documentHandle !== 0) {
        m._capriole_document_close(documentHandle);
        documentHandle = 0;
      }
      const bytes = new Uint8Array(request.data);
      documentHandle = withScratch(m, Math.max(bytes.length, 1), (data) => {
        m.HEAPU8.set(bytes, data);
        return withString(m, request.password, (password) =>
          withScratch(m, 4, (out) => {
            check(m, m._capriole_document_open(data, bytes.length, password, request.repair ? 1 : 0, out));
            return m.HEAPU32[out >> 2]!;
          }),
        );
      });
      return { value: takeJson(m, (out) => m._capriole_document_summary_json(documentHandle, out)), transfer: [] };
    }
    case 'object':
      return {
        value: takeJson(m, (out) => m._capriole_document_object_json(requireDocument(), request.number, out)),
        transfer: [],
      };
    case 'pageContent':
      return {
        value: takeJson(m, (out) =>
          m._capriole_document_page_content_json(requireDocument(), request.index, request.max, out),
        ),
        transfer: [],
      };
    case 'pagePreview':
      return {
        value: takeJson(m, (out) => m._capriole_document_page_preview_json(requireDocument(), request.index, out)),
        transfer: [],
      };
    case 'pageRender': {
      const document = requireDocument();
      const raster = withScratch(m, 12, (out) => {
        check(m, m._capriole_document_page_render(document, request.index, request.dpi, out, out + 4, out + 8));
        const pointer = m.HEAPU32[out >> 2]!;
        const width = m.HEAPU32[(out >> 2) + 1]!;
        const height = m.HEAPU32[(out >> 2) + 2]!;
        try {
          return { width, height, pixels: m.HEAPU8.slice(pointer, pointer + width * height * 4).buffer };
        } finally {
          m._capriole_free(pointer);
        }
      });
      return { value: raster, transfer: [raster.pixels] };
    }
    case 'stream': {
      const data = takeBytes(m, (outData, outSize) =>
        m._capriole_document_stream_data(requireDocument(), request.number, request.decoded ? 1 : 0, outData, outSize),
      );
      return { value: data, transfer: [data] };
    }
    case 'save': {
      const document = requireDocument();
      const save = (date: number) =>
        takeBytes(m, (outData, outSize) =>
          m._capriole_document_save(document, request.incremental ? 1 : 0, date, outData, outSize),
        );
      const data =
        request.modificationDate === null ? save(0) : withString(m, request.modificationDate, (date) => save(date));
      return { value: data, transfer: [data] };
    }
    case 'close':
      if (documentHandle !== 0) {
        m._capriole_document_close(documentHandle);
        documentHandle = 0;
      }
      return { value: null, transfer: [] };
  }
}

self.onmessage = async (event: MessageEvent<{ id: number; request: Request }>) => {
  const { id, request } = event.data;
  try {
    const { value, transfer } = await handle(request);
    self.postMessage({ id, ok: true, value } satisfies Response, { transfer });
  } catch (error) {
    const response: Response =
      error instanceof EngineFailure
        ? { id, ok: false, status: error.status, message: error.message }
        : { id, ok: false, status: Status.internal, message: String(error) };
    self.postMessage(response);
  }
};
