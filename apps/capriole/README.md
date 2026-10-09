# Capriole

The web app (PLAN.md phase 8b): open a PDF and work on its pages — reorder them by dragging (or with the
toolbar's arrows), rotate, duplicate, delete, insert the pages of another PDF, extract the selected
pages into a new file, undo, save. Everything runs in the browser: the engine is the WebAssembly module
in a Web Worker (`packages/engine-client`), the file never leaves the device. A file that needed
repairs is repaired before editing (saved fully and read back in memory), and the app says so.

Where files go: in browsers with the File System Access API (Chrome, Edge) "Scegli un PDF" and "Salva"
use the system's dialogs — "Salva" writes over the file opened (the browser asks once for permission),
or asks where for a file dropped in; "Salva con nome" always asks. In the others (Firefox, Safari) a
saved file is a download, where the browser puts downloads.

```sh
# once: the engine as WebAssembly (from the repository root)
source ~/emsdk/emsdk_env.sh && cmake --preset wasm && cmake --build --preset wasm
cd apps/capriole
npm ci
npm run dev        # http://localhost:5173 — copies the engine in first
npm run build      # static site in build/, to serve from anywhere
npm run check      # svelte-check (type-check)
npm run lint       # prettier --check
npx playwright test   # end-to-end in Chromium (port 4174)
```

**Annotations** (phase 8d): double-click a page (or select it and press "Annota") to show it large.
Tools: highlight, underline and strike out (drag over the text: one mark per line), note (click; its
comment in the side panel), pen, rectangle, ellipse, line, free text, and a colour for the next annotation. Free text is written on the page itself, with the
cursor in a box laid out as the engine will draw it: a click opens a box one line of the chosen size
high that widens as the text is typed; a dragged rectangle keeps its width, wraps the lines at its edge
and grows downwards; a double click on a free text (or a click with the text tool) writes it again,
and emptied it goes. Leaving the box (a click elsewhere, Esc) makes the text the annotation — one step
of undo. The text size is chosen in the toolbar (for the next text, or the one selected). With "Seleziona e sposta", click an annotation to edit its
text or colour or remove it, drag it to move it; the panel also lists the page's annotations (the
keyboard's way: Delete removes the selected one, Escape lets it go, then goes back to the pages). Every
annotation gets its appearance from the engine, so other readers show it the same.

**The page's own text** (phases 8j, 9a–9d): the tool "Modifica testo" writes a line again (a double
click; Enter writes it) or a paragraph, laid out anew inside its box with its margins, alignment and the
styles of its words ("Paragrafo"; Ctrl+Enter). A text that does not fit is offered smaller, or room: what
follows it moved down (not what is beside it, nor a footer; why not, when it cannot be). Letters the
text's font lacks go to a similar standard font, and the side panel says so — unless the font is an
embedded TrueType subset and the person has given the same font under "Font forniti" ("Aggiungi font…",
or dropped there: .ttf, .ttc, .otf with TrueType outlines): the glyphs are then added to the subset from
it, if its licence (OS/2 fsType) allows edited documents. After an edit that needed one, the panel names
the font to give. The fonts given are remembered on the device (IndexedDB; the desktop shell keeps it
in the app's data) and never leave it.

**PDF/A** (phase 8i): "PDF/A" (in the pages' toolbar; it shows the part when the document declares one)
opens a dialog that says what PDF/A is and converts: what was put right is listed (in Italian), or why it
cannot be done, the document left as it was; "Salva il PDF/A" saves the converted document as a new file
("…-pdfa.pdf"). The conversion is one step of undo. A Factur-X invoice whose PDF is not PDF/A-3 has
"Converti in PDF/A-3…" in its panel. A PDF/A document, like a signed one, is saved by appending (a full
save would change /ModDate apart from the XMP).

**Digital signatures** (phase 8h): "Firme" (beside the pages) lists the document's signatures as the
engine verifies them — valid or not, made before later changes or covering the whole file, signer,
time (the time stamp's, else the one the signer claims), PAdES level — and signs: invisibly from there,
or visibly with the page view's "Firma" tool (a box drawn on the page). The dialog takes a PKCS#12 key
file (.p12, .pfx) and its password, reads and shows its certificate first, and a reason and place; "Firma
e salva" signs (a new revision, PAdES B-B) and saves the signed file at once ("…-firmato.pdf"): cancelled,
nothing is signed. A signed document is saved by appending (signatures stay valid; undo snapshots too),
never written anew. The key never leaves the browser; no time stamp (a call to an authority) and no
trusted roots yet: signers' identity shows as not verified.

**Redaction** (phase 8g): the tool "Oscura (redaction)" marks what must go — dragging over text marks
its words, a rectangle elsewhere an area; marks are outlined in red and stay readable until applied
(they are /Redact annotations: listed, selected, removed like the others). "Applica la redaction (n)"
asks for confirmation, then takes what lies in the page's marks out of the file — text, images, drawings,
annotations — and fills the areas black; it is one step of undo until the file is saved, which is a full
save without the previous version.

**Forms** (phase 8e): a page with form fields opens in the page view ready to fill them ("Compila il
modulo"): over each widget the control that fills it — text (one or several lines, password, maximum
length), check box, radio button, menu (with suggestions when the field takes any text), list (several
choices when the field allows). A value is written when the control is left, by the engine with its
appearance, so that every reader shows it; every value goes through undo. The side panel lists the
page's fields with their values (a click goes to the field). Signatures, push buttons and read-only
fields are shown by the page, not filled.

**Attachments and Factur-X** (phase 8f): "Allegati" opens a panel beside the pages: the document's
attachments (its own, and those of file attachment notes, with their page) — downloaded, added (chosen
or dropped in; associated with the document as supplements), removed —, every change through undo. A
document carrying a Factur-X / ZUGFeRD invoice shows it: its profile, and the invoice read from its XML
(number and date, seller and buyer, taxable amount, tax, total, due date). "Crea Factur-X con un XML…"
embeds an invoice's XML (a Cross Industry Invoice) with its XMP metadata; a PDF that is not PDF/A-3 is
said not to be a conforming Factur-X yet (conversion: phase 8i). Below 1900 pixels the page toolbar's
secondary buttons show their icons only (their names stay, for screen readers and as tooltips).

Same stack as the Inspector (ADR 0008, the workspace's frontend standards). Keyboard: Ctrl/Cmd-click
adds a page to the selection, Shift-click a range, Ctrl/Cmd+A selects all, Delete deletes, Ctrl/Cmd+Z
undoes; the arrows in the toolbar move the selected pages. Undo keeps whole copies of the document
(up to 256 MB, the oldest dropped).

Next (phase 8c): the same app as the desktop shell (Tauri).
