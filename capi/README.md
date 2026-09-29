# Capriole C API

`include/capriole/capriole.h` is the engine's only public boundary (ADR 0001): the WebAssembly module,
the desktop shell and third parties go through it. Plain C, opaque handles, a `capriole_status` for
every fallible call, `capriole_last_error_message()` for the details, buffers released with
`capriole_free`. Functions are only ever added; a change or removal bumps `CAPRIOLE_ABI_VERSION`.

## The JSON of the document functions

UTF-8, NUL-terminated. PDF strings and names are **bytes**, not text: each byte is written as the
character with the same code point (U+0000–U+00FF; outside printable ASCII as `\u00XX`), so the bytes
come back exactly (in JavaScript: `charCodeAt(i) & 0xff`). Numbers are strings: int64 values do not all
fit a JavaScript number, and a real keeps the engine's exact text.

A PDF object (`capriole_document_object_json` puts it in `"object"`):

| Kind | JSON |
|---|---|
| null | `{"t":"null"}` |
| boolean | `{"t":"bool","v":true}` |
| integer, real | `{"t":"int","v":"42"}`, `{"t":"real","v":"0.5"}` |
| string | `{"t":"string","form":"literal"\|"hex","v":"<bytes>"}` |
| name | `{"t":"name","v":"<bytes, without the />"}` |
| array | `{"t":"array","v":[…]}` |
| dictionary | `{"t":"dict","v":[["Key",…],…]}` — pairs, in the file's order |
| stream | `{"t":"stream","dict":[…pairs…],"length":N,"offset":N}` (offset −1: data in memory) |
| reference | `{"t":"ref","n":12,"g":0}` |

`capriole_document_object_json`: `{"number","generation","state":"unmodified"|"modified"|"new"|"deleted","object"}`.

`capriole_document_summary_json`:

```json
{
  "version": "1.7", "size": 12345,
  "encryption": null | {"revision":4,"strings":"AES-128","streams":"AES-128","owner":false,"permissions":-4,"encryptMetadata":true},
  "xref": {"startxref":N, "sections":[{"offset":N,"form":"table|stream|hybrid|reconstructed","entries":N}],
           "objects":[[number, "n"|"c"|"f", generation or object stream number], …]},
  "trailer": {"t":"dict", …},
  "pages": [[number, generation], …] | null, "pagesError": "…" (only when pages is null),
  "repairs": [{"kind":"xref_reconstructed","description":"…"}, …]
}
```

Sections are newest first; `"c"` entries are objects inside an object stream.

`capriole_document_page_content_json` (the page's content stream, parsed as a viewer does):

```json
{"page":0, "ref":[12,0], "size":614432, "total":44005, "recovered":0,
 "operations":[{"o":20, "op":"cm", "category":"special_graphics_state", "operands":[…objects…]},
               {"o":90, "op":"BI", "category":"inline_image", "operands":[], "image":{"dict":{…}, "size":64}}, …]}
```

`o` is the operator's offset in the (joined, decoded) content; `category` is the group of Table 50
in snake_case (`unknown` for operators the standard does not define); `total` counts every operation,
`operations` lists at most `max_operations`; `recovered` counts the syntax errors skipped.

`capriole_document_page_preview_json` (the page interpreted, as a vector preview):

```json
{"page":0, "width":595.44, "height":842.4, "rotate":0,
 "commands":{"save":2,"restore":2,"clip":1,"fill":10,"stroke":4,"image":1,"shading":0,"text":12,"group":0,"soft_mask":0},
 "warnings":["font /Calibri is not embedded: a substitute is used", …],
 "missing_resources":["cmap/UniJIS-UCS2-H","font/NotoSansJP-Regular.otf"],
 "svg":"<svg xmlns=… width=… height=…>…</svg>"}
```

`missing_resources` lists what the page wanted from the resource pack (CMaps, CJK and Liberation fonts:
`scripts/fetch_resources.sh`) and was not registered. A host fetches those files, registers each with
`capriole_resource_add(name, data, size)` (process-wide, thread-safe) and draws the page again; after a
render, `capriole_last_missing_resources_json` gives the same list as a JSON array.

`width` and `height` are in points, with /Rotate applied. The SVG is ASCII: text from the document is
written as character references, so it cannot inject markup.

`capriole_document_page_render` draws the same interpreted page with the engine's rasterizer (Blend2D,
ADR 0002) at the requested dpi: `width` × `height` pixels of straight RGBA, rows top to bottom, on an
opaque white page, released with `capriole_free`. Bitmaps above 50 million pixels are refused with
`CAPRIOLE_ERROR_LIMIT_EXCEEDED` (A4 at 600 dpi is 35 million).
