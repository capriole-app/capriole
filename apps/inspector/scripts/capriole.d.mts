// Types of the engine's WebAssembly module (bindings/wasm): the C API of capi/include/capriole/capriole.h
// as Emscripten exports it. Pointers and sizes are numbers (wasm32). Copied next to the generated
// module by scripts/sync-engine.mjs.
export interface CaprioleModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  UTF8ToString(pointer: number): string;
  stringToUTF8(text: string, pointer: number, maxBytes: number): void;
  lengthBytesUTF8(text: string): number;
  _malloc(size: number): number;
  _free(pointer: number): void;
  _capriole_version(): number;
  _capriole_last_error_message(): number;
  _capriole_free(pointer: number): void;
  _capriole_document_open(data: number, size: number, password: number, repair: number, out: number): number;
  _capriole_document_close(document: number): void;
  _capriole_document_summary_json(document: number, out: number): number;
  _capriole_document_object_json(document: number, objectNumber: number, out: number): number;
  _capriole_document_page_content_json(document: number, pageIndex: number, maxOperations: number, out: number): number;
  _capriole_document_stream_data(
    document: number,
    objectNumber: number,
    decoded: number,
    outData: number,
    outSize: number,
  ): number;
  _capriole_document_save(
    document: number,
    incremental: number,
    modificationDate: number,
    outData: number,
    outSize: number,
  ): number;
}

export default function createCapriole(options?: Record<string, unknown>): Promise<CaprioleModule>;
