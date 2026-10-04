// Types of the engine's WebAssembly module (bindings/wasm): the C API of capi/include/capriole/capriole.h
// as Emscripten exports it. Pointers and sizes are numbers (wasm32). Copied next to the generated
// module by scripts/sync-engine.mjs.
export interface CaprioleModule {
  HEAPU8: Uint8Array;
  HEAP32: Int32Array;
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
  _capriole_document_page_preview_json(document: number, pageIndex: number, out: number): number;
  _capriole_document_page_render(
    document: number,
    pageIndex: number,
    dpi: number,
    outRgba: number,
    outWidth: number,
    outHeight: number,
  ): number;
  _capriole_document_page_text_json(document: number, pageIndex: number, order: number, out: number): number;
  _capriole_document_stream_data(
    document: number,
    objectNumber: number,
    decoded: number,
    outData: number,
    outSize: number,
  ): number;
  _capriole_resource_add(name: number, data: number, size: number): number;
  _capriole_last_missing_resources_json(out: number): number;
  _capriole_document_pages_rotate(document: number, pages: number, count: number, degrees: number): number;
  _capriole_document_pages_arrange(document: number, order: number, count: number): number;
  _capriole_document_pages_import(document: number, source: number, pages: number, count: number, at: number): number;
  _capriole_document_page_annotations_json(document: number, pageIndex: number, out: number): number;
  _capriole_document_annotation_add(
    document: number,
    pageIndex: number,
    spec: number,
    outNumber: number,
    outGeneration: number,
  ): number;
  _capriole_document_annotation_update(
    document: number,
    pageIndex: number,
    num: number,
    generation: number,
    change: number,
  ): number;
  _capriole_document_annotation_remove(document: number, pageIndex: number, num: number, generation: number): number;
  _capriole_document_form_fields_json(document: number, out: number): number;
  _capriole_document_form_field_fill(document: number, num: number, generation: number, value: number): number;
  _capriole_document_attachments_json(document: number, out: number): number;
  _capriole_document_attachment_data(
    document: number,
    num: number,
    generation: number,
    outData: number,
    outSize: number,
    outChecksum: number,
  ): number;
  _capriole_document_attachment_add(
    document: number,
    spec: number,
    data: number,
    size: number,
    outNumber: number,
    outGeneration: number,
  ): number;
  _capriole_document_attachment_remove(document: number, num: number, generation: number): number;
  _capriole_document_facturx_json(document: number, out: number): number;
  _capriole_document_facturx_embed(document: number, xml: number, size: number, date: number, out: number): number;
  _capriole_document_page_redact(document: number, pageIndex: number, spec: number, out: number): number;
  _capriole_document_page_apply_redactions(document: number, pageIndex: number, out: number): number;
  _capriole_pkcs12_info(p12: number, size: number, password: number, out: number): number;
  _capriole_document_sign_begin(
    document: number,
    spec: number,
    p12: number,
    p12Size: number,
    password: number,
    outSigning: number,
  ): number;
  _capriole_signing_timestamp_request(signing: number, outData: number, outSize: number): number;
  _capriole_document_sign_finish(
    document: number,
    signing: number,
    response: number,
    responseSize: number,
    outData: number,
    outSize: number,
  ): number;
  _capriole_signing_free(signing: number): void;
  _capriole_document_signatures_json(document: number, anchors: number, anchorsSize: number, out: number): number;
  _capriole_document_save_compact(document: number, modificationDate: number, outData: number, outSize: number): number;
  _capriole_document_save(
    document: number,
    incremental: number,
    modificationDate: number,
    outData: number,
    outSize: number,
  ): number;
}

export default function createCapriole(options?: Record<string, unknown>): Promise<CaprioleModule>;
