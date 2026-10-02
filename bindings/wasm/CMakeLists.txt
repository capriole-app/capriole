# WebAssembly module: the C API exported to JavaScript as an ES module factory.
#
#   import createCapriole from './capriole.mjs';
#   const m = await createCapriole();
#   m.UTF8ToString(m._capriole_version());

add_executable(capriole_wasm capriole_wasm.cpp)
set_target_properties(capriole_wasm PROPERTIES OUTPUT_NAME capriole SUFFIX ".mjs")
target_link_libraries(capriole_wasm PRIVATE capriole::capi)
capriole_set_warnings(capriole_wasm)

set(_capriole_exports
  _capriole_version
  _capriole_abi_version
  _capriole_last_error_message
  _capriole_probe_header
  _capriole_document_open
  _capriole_document_close
  _capriole_free
  _capriole_document_summary_json
  _capriole_document_object_json
  _capriole_document_page_content_json
  _capriole_document_page_preview_json
  _capriole_document_page_render
  _capriole_document_page_text_json
  _capriole_document_stream_data
  _capriole_document_save
  _capriole_document_save_compact
  _capriole_document_pages_rotate
  _capriole_document_pages_arrange
  _capriole_document_pages_import
  _capriole_document_page_annotations_json
  _capriole_document_annotation_add
  _capriole_document_annotation_update
  _capriole_document_annotation_remove
  _capriole_resource_add
  _capriole_last_missing_resources_json
  _malloc
  _free)
list(JOIN _capriole_exports "," _capriole_exports)

target_link_options(capriole_wasm PRIVATE
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sEXPORT_NAME=createCapriole"
  "-sENVIRONMENT=web,worker,node"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sMAXIMUM_MEMORY=4GB"
  "-sEXPORTED_FUNCTIONS=${_capriole_exports}"
  "-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAP32,HEAPU32")

if(CAPRIOLE_BUILD_TESTS)
  add_test(NAME wasm_module_smoke
    COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${CMAKE_CURRENT_SOURCE_DIR}/smoke.test.mjs $<TARGET_FILE:capriole_wasm>)
endif()
