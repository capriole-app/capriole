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
 "commands":{"save":2,"restore":2,"clip":1,"fill":10,"stroke":4,"image":1,"shading":0,"pattern":0,"text":12,"group":0,"soft_mask":0},
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

`capriole_document_page_text_json` (the page's text, phase 7; ADR 0007):

```json
{"page":0, "width":100.00, "height":50.00, "order":"content", "text":"Caf\u00e9 \u20ac1\n",
 "blocks":[[x0,y0,x1,y1, first line, line count], …],
 "lines":[[x0,y0,x1,y1, first word, word count], …],
 "words":[[x0,y0,x1,y1, first char, char count], …],
 "chars":[["\u00e9", x0,y0,x1,y1, size, angle, invisible, stream, offset, operation, byte, font], …],
 "warnings":[…], "missing_resources":[…]}
```

Coordinates are points on the page as shown — origin at the top left of the crop box, y down, /Rotate
applied, the same as a rendering at 72 dpi — as JSON numbers (two decimals). `text` is the plain text in
reading order (words separated by spaces, lines by `\n`, blocks by an empty line); blocks, lines and
words are consecutive ranges of the next level, in the same order, and `chars` are in reading order. A
character: its text (one character, a few for a ligature, the whole `/ActualText` of a marked sequence,
U+FFFD when the font says nothing), its box, the font size as shown, the angle of its baseline (degrees
clockwise; 90 for text running down), 1 when invisible (render mode 3 or 7, the text layer of a scan),
and where it comes from: `stream` (`null` for the page's `/Contents`, joined; `[number, generation]`
for a form XObject), the offset and ordinal (from 0) of the text operator in that stream — the `o` of
`capriole_document_page_content_json` for the page's own content —, the byte of its code within the
operator's strings, and the font dictionary (`[number, generation]` or `null`). `order`:
`CAPRIOLE_TEXT_ORDER_CONTENT` (the content's order, the default) or `CAPRIOLE_TEXT_ORDER_LAYOUT` (by
columns). Unlike the other functions, the text here is Unicode: the JSON is ASCII with `\uXXXX` escapes.

`capriole_document_page_render` draws the same interpreted page with the engine's rasterizer (Blend2D,
ADR 0002) at the requested dpi: `width` × `height` pixels of straight RGBA, rows top to bottom, on an
opaque white page, released with `capriole_free`. Bitmaps above 50 million pixels are refused with
`CAPRIOLE_ERROR_LIMIT_EXCEEDED` (A4 at 600 dpi is 35 million).

## Page operations

`capriole_document_pages_rotate`, `capriole_document_pages_arrange` and `capriole_document_pages_import`
change the open document in memory (engine: `edit/pages.hpp`, PLAN.md 8a); a save keeps the result.
Rotation adds a multiple of 90° to each page's rotation and touches only those pages. Arranging lists
the pages to keep, in order, by their current index: left out means deleted, listed twice means
duplicated. Importing copies pages of another open document, with what they use (contents, resources,
annotations; form fields join the form, renamed when their name is taken), before page `at`.
Merging is an import of every page at the end; extracting is an arrangement followed by
`capriole_document_save_compact`, a full save that leaves out the objects nothing reaches any more;
splitting is extracting from fresh copies of the document.

Arranging and importing write the page tree anew, flat (the root lists every page); pages that move
from under an intermediate node take the attributes they inherited from it. References to deleted
pages (outline items, links) read as null afterwards.

## Annotations

`capriole_document_page_annotations_json` lists a page's annotations (popups left out, they belong to
their parent):

```json
[{"ref":[12,0], "subtype":"Highlight", "rect":[x0,y0,x1,y1], "contents":"…", "author":"…",
  "color":[1.000,0.820,0.000] | null, "hidden":false, "editable":true}, …]
```

`ref` is `null` for an annotation written directly in `/Annots` (listed, not editable); `editable` is
false for those and for form widgets (they change with their fields: see Forms). Coordinates are those of the page as shown, as for
text (points, top-left origin, /Rotate applied); a note (NoRotate) is given where it is shown, upright.
`contents` and `author` are Unicode (`\u` escapes).

`capriole_document_annotation_add` takes a specification in JSON (UTF-8 text; unknown keys ignored, a
known key with a value of the wrong shape is an error, as is malformed JSON — nested at most 32 deep):

```json
{"kind":"highlight"|"underline"|"strikeout"|"squiggly"|"note"|"ink"|"square"|"circle"|"line"|"freetext",
 "quads":[[x0,y0,x1,y1], …],          // text markup: boxes of horizontal text (e.g. words of the page's text)
 "strokes":[[[x,y], …], …],           // ink
 "rect":[x0,y0,x1,y1],                // note (its icon), square, circle, free text
 "from":[x,y], "to":[x,y],            // line
 "color":[r,g,b], "fill":[r,g,b],     // 0..1; fill: the interior of a square or circle
 "width":1, "opacity":1, "fontSize":12,
 "contents":"…", "author":"…", "date":"D:20261002120000Z"}
```

The annotation gets its appearance (/AP), made by the engine, so that every reader draws it as
Capriole does: markup from its geometry, a note's icon (NoZoom and NoRotate, as Acrobat writes notes),
free text in Helvetica wrapped to its rectangle, line breaks kept (Latin text: characters outside WinAnsi
become `?`).
`capriole_document_annotation_update` takes `{"moveBy":[dx,dy], "contents":"…", "color":[r,g,b],
"opacity":n, "date":"…"}` (any of them); a new colour or text makes the appearance again (for the kinds
the engine makes; another kind's colour is `CAPRIOLE_ERROR_UNSUPPORTED`). `capriole_document_annotation_remove`
removes it with its popup. Documents whose structure needed repairs are edited after a full save
(`CAPRIOLE_ERROR_UNSUPPORTED` until then); listing works on any document.

## Forms

`capriole_document_form_fields_json` lists the terminal fields of the document's form (AcroForm), in
the order of the field tree (`[]` without a form):

```json
[{"ref":[10,0], "name":"person.address", "label":"…",
  "type":"text"|"checkbox"|"radio"|"pushbutton"|"combobox"|"listbox"|"signature",
  "value":["…"], "options":[{"value":"it","display":"Italiano"}, …], "maxLength":20 | null,
  "readOnly":false, "required":false, "multiline":false, "password":false, "comb":false,
  "editable":false, "multiSelect":false, "fillable":true,
  "widgets":[{"ref":[10,0], "page":0 | null, "rect":[x0,y0,x1,y1], "onState":"Yes"}, …]}, …]
```

`name` is fully qualified (partial names joined with `.`); text is Unicode (`\u` escapes). `value` is an
array: one string for text and combo boxes (none when empty), the chosen options of a list box, the
state that is on for check boxes and radio buttons (`"Off"` when none). `options` are a choice's options
(the value `/V` holds, the text shown) or a button's on states (shown by their `/Opt` export value).
A widget's `rect` is on its page as shown, as for annotations (`page` null when no page lists it: its
`/Rect` as it is); `onState` is a button widget's on state. `fillable` is false for read-only fields,
signatures (phase 8h) and push buttons.

`capriole_document_form_field_fill(document, number, generation, value_json)` fills a field: `"text"`,
`["a","b"]` (list boxes; one string otherwise), or `null` to clear it. Text: line breaks become spaces in
a single-line field, at most `maxLength` characters. Check box: its on state or `"Off"`; radio button: a
widget's on state (widgets sharing it come on together) or `"Off"`; combo box: an option's value, or any
text when editable; list box: option values, several when multi-select. A value the field does not take
is `CAPRIOLE_ERROR_OUT_OF_RANGE`, as is a reference that is not a terminal field; a field that is not
fillable is `CAPRIOLE_ERROR_UNSUPPORTED`.

Each widget gets the appearance of the value, made by the engine (§12.7.4.3): text and choices in the
field's font (`/DA` in the form's `/DR`) when it is a simple font with a glyph for every character,
otherwise in Helvetica (WinAnsi); auto size, alignment (`/Q`), comb cells, password asterisks, several
lines, the widget's background, border and rotation (`/MK`); a list box shows its options with the
chosen ones highlighted. Check boxes and radio buttons turn on their state (`/AS`), with a drawn mark
when the widget has no appearance states. A form with XFA loses it when filled (readers that know XFA
would show it instead). Documents whose structure needed repairs are filled after a full save.
