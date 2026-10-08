// The engine in a worker: loads the WebAssembly module and answers the page's requests through the
// C API. One document open at a time.
import createCapriole, { type CaprioleModule } from './generated/capriole.mjs';
import { Status, type Request, type Response, type Summary } from './types';

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
/** The open document's password: copies of it (extracting pages) are saved encrypted alike. */
let documentPassword = '';

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

/** Runs fn with a key file's bytes in the module's memory, wiped before it is freed. */
function withSecret<T>(m: CaprioleModule, bytes: Uint8Array, fn: (pointer: number, size: number) => T): T {
  return withScratch(m, Math.max(bytes.length, 1), (pointer) => {
    m.HEAPU8.set(bytes, pointer);
    bytes.fill(0);
    try {
      return fn(pointer, bytes.length);
    } finally {
      m.HEAPU8.fill(0, pointer, pointer + bytes.length);
    }
  });
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

// The resource pack (ADR 0005): fonts and CMaps the engine asks for when a page needs them, fetched from
// resourceBase once each (a name that is not there is not asked for again) and registered in the module.
let resourceBase: string | null = null;
const attempted = new Set<string>();

/** Registers what the last page missed and was not tried yet; true when anything was added. */
async function supplyMissing(m: CaprioleModule): Promise<boolean> {
  if (resourceBase === null) return false;
  const missing = takeJson(m, (out) => m._capriole_last_missing_resources_json(out)) as string[];
  const wanted = missing.filter((name) => !attempted.has(name));
  if (wanted.length === 0) return false;
  for (const name of wanted) attempted.add(name);
  const base = resourceBase;
  const fetched = await Promise.all(
    wanted.map(async (name) => {
      try {
        const response = await fetch(new URL(name, base));
        return response.ok ? { name, data: new Uint8Array(await response.arrayBuffer()) } : null;
      } catch {
        return null; // offline, or not served: the page is drawn without it
      }
    }),
  );
  let added = false;
  for (const resource of fetched) {
    if (resource === null) continue;
    withString(m, resource.name, (name) =>
      withScratch(m, Math.max(resource.data.length, 1), (data) => {
        m.HEAPU8.set(resource.data, data);
        check(m, m._capriole_resource_add(name, data, resource.data.length));
      }),
    );
    added = true;
  }
  return added;
}

/** Opens bytes as a document (repairs on) and returns its handle. */
function openBytes(m: CaprioleModule, bytes: Uint8Array, password: string): number {
  return withScratch(m, Math.max(bytes.length, 1), (data) => {
    m.HEAPU8.set(bytes, data);
    return withString(m, password, (passwordPointer) =>
      withScratch(m, 4, (out) => {
        check(m, m._capriole_document_open(data, bytes.length, passwordPointer, 1, out));
        return m.HEAPU32[out >> 2]!;
      }),
    );
  });
}

/** Saves a document: full (incremental 0), or compact. */
function saveBytes(m: CaprioleModule, handle: number, compact: boolean, date: string | null): ArrayBuffer {
  const save = (datePointer: number) =>
    takeBytes(m, (outData, outSize) =>
      compact
        ? m._capriole_document_save_compact(handle, datePointer, outData, outSize)
        : m._capriole_document_save(handle, 0, datePointer, outData, outSize),
    );
  return date === null ? save(0) : withString(m, date, save);
}

/** Calls fn with page indices as a uint32_t array in the module's memory. */
function withIndices<T>(m: CaprioleModule, indices: number[], fn: (pointer: number) => T): T {
  return withScratch(m, Math.max(indices.length, 1) * 4, (pointer) => {
    m.HEAPU32.set(indices, pointer >> 2);
    return fn(pointer);
  });
}

function summaryOf(m: CaprioleModule, handle: number): unknown {
  return takeJson(m, (out) => m._capriole_document_summary_json(handle, out));
}

function replaceDocument(m: CaprioleModule, handle: number): void {
  if (documentHandle !== 0) m._capriole_document_close(documentHandle);
  documentHandle = handle;
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
      documentPassword = request.password;
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
    case 'resourceBase':
      resourceBase = request.base;
      return { value: null, transfer: [] };
    case 'pagePreview': {
      const preview = () =>
        takeJson(m, (out) => m._capriole_document_page_preview_json(requireDocument(), request.index, out));
      let value = preview();
      if (await supplyMissing(m)) value = preview();
      return { value, transfer: [] };
    }
    case 'pageRender': {
      const document = requireDocument();
      const render = () =>
        withScratch(m, 12, (out) => {
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
      let raster = render();
      if (await supplyMissing(m)) raster = render();
      return { value: raster, transfer: [raster.pixels] };
    }
    case 'pageText': {
      const order = request.order === 'layout' ? 1 : 0; // CAPRIOLE_TEXT_ORDER_*
      const text = () =>
        takeJson(m, (out) => m._capriole_document_page_text_json(requireDocument(), request.index, order, out));
      let value = text();
      if (await supplyMissing(m)) value = text();
      return { value, transfer: [] };
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
    case 'openForEditing': {
      const opened = openBytes(m, new Uint8Array(request.data), request.password);
      replaceDocument(m, opened);
      documentPassword = request.password;
      let summary = summaryOf(m, opened) as Summary;
      const repairs = summary.repairs;
      if (repairs.length > 0) {
        // What the repairs found is written out, and the document read back from it is the one edited.
        const sound = openBytes(m, new Uint8Array(saveBytes(m, opened, false, null)), request.password);
        replaceDocument(m, sound);
        summary = summaryOf(m, sound) as Summary;
      }
      return { value: { summary, repairs }, transfer: [] };
    }
    case 'pagesRotate': {
      const document = requireDocument();
      withIndices(m, request.pages, (pages) =>
        check(m, m._capriole_document_pages_rotate(document, pages, request.pages.length, request.degrees)),
      );
      return { value: summaryOf(m, document), transfer: [] };
    }
    case 'pagesArrange': {
      const document = requireDocument();
      withIndices(m, request.order, (order) =>
        check(m, m._capriole_document_pages_arrange(document, order, request.order.length)),
      );
      return { value: summaryOf(m, document), transfer: [] };
    }
    case 'importFile': {
      const document = requireDocument();
      const source = openBytes(m, new Uint8Array(request.data), request.password);
      try {
        const pages = (summaryOf(m, source) as Summary).pages ?? [];
        withIndices(
          m,
          pages.map((_, i) => i),
          (indices) => check(m, m._capriole_document_pages_import(document, source, indices, pages.length, request.at)),
        );
      } finally {
        m._capriole_document_close(source);
      }
      return { value: summaryOf(m, document), transfer: [] };
    }
    case 'extract': {
      // On a copy: the document as it is now, saved and read back, keeps only these pages.
      const copy = openBytes(m, new Uint8Array(saveBytes(m, requireDocument(), false, null)), documentPassword);
      try {
        withIndices(m, request.pages, (pages) =>
          check(m, m._capriole_document_pages_arrange(copy, pages, request.pages.length)),
        );
        const data = saveBytes(m, copy, true, null);
        return { value: data, transfer: [data] };
      } finally {
        m._capriole_document_close(copy);
      }
    }
    case 'saveCompact': {
      const data = saveBytes(m, requireDocument(), true, request.modificationDate);
      return { value: data, transfer: [data] };
    }
    case 'annotations':
      return {
        value: takeJson(m, (out) => m._capriole_document_page_annotations_json(requireDocument(), request.index, out)),
        transfer: [],
      };
    case 'annotationAdd': {
      const document = requireDocument();
      const ref = withString(m, JSON.stringify(request.spec), (spec) =>
        withScratch(m, 8, (out) => {
          check(m, m._capriole_document_annotation_add(document, request.index, spec, out, out + 4));
          return [m.HEAPU32[out >> 2]!, m.HEAPU32[(out >> 2) + 1]!] as [number, number];
        }),
      );
      return { value: ref, transfer: [] };
    }
    case 'annotationUpdate': {
      const document = requireDocument();
      withString(m, JSON.stringify(request.change), (change) =>
        check(
          m,
          m._capriole_document_annotation_update(document, request.index, request.ref[0], request.ref[1], change),
        ),
      );
      return { value: null, transfer: [] };
    }
    case 'attachments':
      return {
        value: takeJson(m, (out) => m._capriole_document_attachments_json(requireDocument(), out)),
        transfer: [],
      };
    case 'attachmentData': {
      const document = requireDocument();
      let checksum = -1;
      const data = withScratch(m, 4, (outChecksum) => {
        const bytes = takeBytes(m, (outData, outSize) =>
          m._capriole_document_attachment_data(document, request.ref[0], request.ref[1], outData, outSize, outChecksum),
        );
        checksum = m.HEAP32[outChecksum >> 2]!;
        return bytes;
      });
      return { value: { data, checksum: checksum < 0 ? null : checksum === 1 }, transfer: [data] };
    }
    case 'attachmentAdd': {
      const document = requireDocument();
      const bytes = new Uint8Array(request.data);
      const ref = withScratch(m, Math.max(bytes.length, 1), (data) => {
        m.HEAPU8.set(bytes, data);
        return withString(m, JSON.stringify(request.spec), (spec) =>
          withScratch(m, 8, (out) => {
            check(m, m._capriole_document_attachment_add(document, spec, data, bytes.length, out, out + 4));
            return [m.HEAPU32[out >> 2]!, m.HEAPU32[(out >> 2) + 1]!] as [number, number];
          }),
        );
      });
      return { value: ref, transfer: [] };
    }
    case 'attachmentRemove':
      check(m, m._capriole_document_attachment_remove(requireDocument(), request.ref[0], request.ref[1]));
      return { value: null, transfer: [] };
    case 'facturx':
      return { value: takeJson(m, (out) => m._capriole_document_facturx_json(requireDocument(), out)), transfer: [] };
    case 'facturxEmbed': {
      const document = requireDocument();
      const bytes = new Uint8Array(request.xml);
      const result = withScratch(m, Math.max(bytes.length, 1), (xml) => {
        m.HEAPU8.set(bytes, xml);
        return withString(m, request.date, (date) =>
          takeJson(m, (out) => m._capriole_document_facturx_embed(document, xml, bytes.length, date, out)),
        );
      });
      return { value: result, transfer: [] };
    }
    case 'applyRedactions':
      return {
        value: takeJson(m, (out) => m._capriole_document_page_apply_redactions(requireDocument(), request.index, out)),
        transfer: [],
      };
    case 'replaceText':
      return {
        value: withString(m, JSON.stringify({ area: request.area, text: request.text }), (spec) =>
          takeJson(m, (out) => m._capriole_document_page_replace_text(requireDocument(), request.index, spec, out)),
        ),
        transfer: [],
      };
    case 'replaceParagraph':
      return {
        value: withString(
          m,
          JSON.stringify({ area: request.area, text: request.text, shrink: request.shrink }),
          (spec) =>
            takeJson(m, (out) =>
              m._capriole_document_page_replace_paragraph(requireDocument(), request.index, spec, out),
            ),
        ),
        transfer: [],
      };
    case 'fontAdd': {
      const bytes = new Uint8Array(request.data);
      const value = withScratch(m, Math.max(bytes.length, 1), (data) => {
        m.HEAPU8.set(bytes, data);
        return takeJson(m, (out) => m._capriole_font_add(data, bytes.length, out));
      });
      return { value, transfer: [] };
    }
    case 'fonts':
      return { value: takeJson(m, (out) => m._capriole_fonts_json(out)), transfer: [] };
    case 'fontsClear':
      check(m, m._capriole_fonts_clear());
      return { value: null, transfer: [] };
    case 'pkcs12Info':
      return {
        value: withSecret(m, new Uint8Array(request.p12), (p12, size) =>
          withString(m, request.password, (password) =>
            takeJson(m, (out) => m._capriole_pkcs12_info(p12, size, password, out)),
          ),
        ),
        transfer: [],
      };
    case 'sign': {
      const document = requireDocument();
      const signing = withSecret(m, new Uint8Array(request.p12), (p12, size) =>
        withString(m, JSON.stringify(request.spec), (spec) =>
          withString(m, request.password, (password) =>
            withScratch(m, 4, (out) => {
              check(m, m._capriole_document_sign_begin(document, spec, p12, size, password, out));
              return m.HEAPU32[out >> 2]!;
            }),
          ),
        ),
      );
      try {
        // The C API reopens the document from the signed file: the handle stays.
        const data = takeBytes(m, (outData, outSize) =>
          m._capriole_document_sign_finish(document, signing, 0, 0, outData, outSize),
        );
        return { value: data, transfer: [data] };
      } finally {
        m._capriole_signing_free(signing);
      }
    }
    case 'convertPdfa': {
      const document = requireDocument();
      const report = withString(m, JSON.stringify({ part: request.part, date: request.date }), (spec) =>
        withScratch(m, 12, (out) => {
          check(m, m._capriole_document_convert_pdfa(document, spec, out, out + 4, out + 8));
          const data = m.HEAPU32[out >> 2]!;
          const json = m.HEAPU32[(out >> 2) + 2]!;
          m._capriole_free(data); // the file: the document is it now, saved from it when asked
          try {
            return JSON.parse(m.UTF8ToString(json));
          } finally {
            m._capriole_free(json);
          }
        }),
      );
      return { value: report, transfer: [] };
    }
    case 'summary':
      return { value: summaryOf(m, requireDocument()), transfer: [] };
    case 'signatures':
      return {
        value: takeJson(m, (out) => m._capriole_document_signatures_json(requireDocument(), 0, 0, out)),
        transfer: [],
      };
    case 'formFields':
      return {
        value: takeJson(m, (out) => m._capriole_document_form_fields_json(requireDocument(), out)),
        transfer: [],
      };
    case 'formFill': {
      const document = requireDocument();
      withString(m, JSON.stringify(request.value), (value) =>
        check(m, m._capriole_document_form_field_fill(document, request.ref[0], request.ref[1], value)),
      );
      return { value: null, transfer: [] };
    }
    case 'annotationRemove':
      check(
        m,
        m._capriole_document_annotation_remove(requireDocument(), request.index, request.ref[0], request.ref[1]),
      );
      return { value: null, transfer: [] };
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
