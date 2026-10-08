// The JSON the engine returns (capi/README.md), and the messages between the page and the worker.

/** A PDF object. Strings and names carry bytes as characters U+0000–U+00FF; numbers as text. */
export type PdfValue =
  | { t: 'null' }
  | { t: 'bool'; v: boolean }
  | { t: 'int'; v: string }
  | { t: 'real'; v: string }
  | { t: 'string'; form: 'literal' | 'hex'; v: string }
  | { t: 'name'; v: string }
  | { t: 'array'; v: PdfValue[] }
  | { t: 'dict'; v: [string, PdfValue][] }
  | { t: 'stream'; dict: [string, PdfValue][]; length: number; offset: number }
  | { t: 'ref'; n: number; g: number };

export type PdfDict = [string, PdfValue][];

export interface Encryption {
  revision: number;
  strings: string;
  streams: string;
  owner: boolean;
  permissions: number;
  encryptMetadata: boolean;
}

export interface XrefSection {
  offset: number;
  form: 'table' | 'stream' | 'hybrid' | 'reconstructed';
  entries: number;
}

/** [number, 'n' in use | 'c' in an object stream | 'f' free, generation or object stream number] */
export type XrefObject = [number, 'n' | 'c' | 'f', number];

export interface Repair {
  kind: string;
  description: string;
}

export interface Summary {
  version: string;
  size: number;
  encryption: Encryption | null;
  xref: { startxref: number; sections: XrefSection[]; objects: XrefObject[] };
  trailer: Extract<PdfValue, { t: 'dict' }>;
  pages: [number, number][] | null;
  pagesError?: string;
  /** The PDF/A part the document declares (its XMP); null when none. */
  pdfa: number | null;
  repairs: Repair[];
}

/** What a conversion to PDF/A put right (capriole_document_convert_pdfa). */
export interface PdfaReport {
  part: number;
  changes: string[];
}

export interface ObjectInfo {
  number: number;
  generation: number;
  state: 'unmodified' | 'modified' | 'new' | 'deleted';
  object: PdfValue;
}

export interface ContentOperation {
  /** Offset of the operator in the page's (joined, decoded) content. */
  o: number;
  op: string;
  category: string;
  operands: PdfValue[];
  image?: { dict: Extract<PdfValue, { t: 'dict' }>; size: number };
}

export interface PageContent {
  page: number;
  ref: [number, number];
  size: number;
  total: number;
  recovered: number;
  operations: ContentOperation[];
}

export interface PagePreview {
  page: number;
  /** In points, /Rotate applied. */
  width: number;
  height: number;
  rotate: number;
  commands: Record<
    'save' | 'restore' | 'clip' | 'fill' | 'stroke' | 'image' | 'shading' | 'pattern' | 'text' | 'group' | 'soft_mask',
    number
  >;
  warnings: string[];
  /** What the page wanted from the resource pack and did not find (the worker fetches it first). */
  missing_resources: string[];
  svg: string;
}

/** [x0, y0, x1, y1, first, count]: a box in points (top-left origin), and a range of the next level. */
export type TextRange = [number, number, number, number, number, number];

/**
 * A character: text, box (x0, y0, x1, y1), size, angle, invisible (0/1), stream ([number, generation],
 * null for the page's content), operator offset, operator ordinal, byte of its code, font.
 */
export type TextChar = [
  string,
  number,
  number,
  number,
  number,
  number,
  number,
  0 | 1,
  [number, number] | null,
  number,
  number,
  number,
  [number, number] | null,
];

export type TextOrder = 'content' | 'layout';

export interface PageText {
  page: number;
  /** In points, /Rotate applied. */
  width: number;
  height: number;
  order: TextOrder;
  text: string;
  blocks: TextRange[];
  lines: TextRange[];
  words: TextRange[];
  chars: TextChar[];
  warnings: string[];
  missing_resources: string[];
}

/** An annotation of a page, as the engine lists it (capi/README.md). Coordinates: the page as shown, points. */
export interface AnnotationInfo {
  ref: [number, number] | null;
  subtype: string;
  rect: [number, number, number, number];
  contents: string;
  author: string;
  /** /C; for free text, its text's colour. */
  color: [number, number, number] | null;
  /** Free text: its text's size (points); null for the other kinds. */
  fontSize: number | null;
  hidden: boolean;
  editable: boolean;
}

export type AnnotationKind =
  | 'highlight'
  | 'underline'
  | 'strikeout'
  | 'squiggly'
  | 'note'
  | 'ink'
  | 'square'
  | 'circle'
  | 'line'
  | 'freetext'
  /** A redaction mark: rect, or quads of text; fill: what the area becomes once applied. */
  | 'redact';

/** A certificate, as the engine reads it (dates are PDF dates, UTC). */
export interface CertificateInfo {
  name: string;
  subject: string;
  issuer: string;
  serial: string;
  notBefore: string;
  notAfter: string;
  key: string;
  ca: boolean;
  /** pkcs12Info: how many other certificates the file has. */
  chain?: number;
}

/** A signature of the document, verified (capi/README.md). */
export interface SignatureInfo {
  field: [number, number];
  fieldName: string;
  subFilter: string;
  documentTimestamp: boolean;
  name: string;
  reason: string;
  location: string;
  contactInfo: string;
  /** /M, as written. */
  date: string;
  integrity: 'valid' | 'invalid' | 'unsupported';
  problem: string;
  digest: string | null;
  /** False: revisions were added after it (later changes, or other signatures). */
  coversWholeFile: boolean;
  signedBytes: number;
  signer: CertificateInfo | null;
  level: string;
  timestamp: string;
  timestampInvalid: boolean;
  /** The time stamp's time when there is one, else /M (what the signer claims). */
  signingTime: string;
  certificateValidAtSigning: boolean;
  trust: 'trusted' | 'untrusted' | 'notChecked';
  trustProblem: string;
  page: number | null;
  rect: [number, number, number, number] | null;
}

/** What a signature is made with (capriole_document_sign_begin). */
export interface SignSpec {
  /** /M, a PDF date. */
  date: string;
  page?: number;
  /** As shown; null: an invisible signature. */
  rect?: [number, number, number, number] | null;
  /** A visible signature's text, one line each ("{signer}": the certificate's name). */
  lines?: string[];
  fieldName?: string;
  name?: string;
  reason?: string;
  location?: string;
  contactInfo?: string;
}

/** How capriole_document_page_replace_text wrote the new text (capi/README.md). */
export interface TextEditReport {
  /** sameFont: the old text's font; similarFont: the nearest standard font, drawn where the old glyphs were. */
  method: 'sameFont' | 'similarFont';
  font: string;
  /** The text that was there. */
  replaced: string;
  /** similarFont: the horizontal scaling it is written with (1: none; at least 0.5). */
  scale: number;
  /** Glyphs added to the embedded font from a font given with fontAdd. */
  addedGlyphs: number;
  /** similarFont: the embedded font that lacked letters, when a font given could add them ("": none). */
  missingFont: string;
}

/** How capriole_document_page_replace_paragraph laid the new text out (capi/README.md). */
export interface ParagraphEditReport {
  method: 'sameFont' | 'similarFont';
  font: string;
  /** The text that was there, its lines joined by spaces. */
  replaced: string;
  oldLines: number;
  lines: number;
  /** The font size it is written in (points as shown), and that over the paragraph's (below 1: shrunk). */
  size: number;
  scale: number;
  alignment: 'left' | 'justified' | 'centre' | 'right';
  /** The styles (fonts, sizes, colours) the new text keeps from the old, and of them those all in their own font. */
  styles: number;
  sameFontStyles: number;
  /** Glyphs added to the embedded fonts from fonts given with fontAdd. */
  addedGlyphs: number;
  /** The embedded fonts that lacked letters (written in a similar font) and that a font given could extend. */
  missingFonts: string[];
}

/** A face of a font given with fontAdd. */
export interface FontFace {
  postscriptName: string;
  family: string;
  style: string;
  /** Its embedding permissions (OS/2 fsType) allow its glyphs in edited documents. */
  embeddable: boolean;
}

/** What a redaction took out of a page. */
export interface RedactionReport {
  glyphs: number;
  paths: number;
  imagesRemoved: number;
  imagesCleared: number;
  annotations: number;
}

/** What capriole_document_annotation_add takes (capi/README.md). */
export interface AnnotationSpec {
  kind: AnnotationKind;
  quads?: [number, number, number, number][];
  strokes?: [number, number][][];
  rect?: [number, number, number, number];
  from?: [number, number];
  to?: [number, number];
  color?: [number, number, number];
  fill?: [number, number, number];
  width?: number;
  opacity?: number;
  fontSize?: number;
  contents?: string;
  author?: string;
  date?: string;
}

export interface AnnotationChange {
  moveBy?: [number, number];
  /** A new rectangle (the page as shown): free text, square, circle. */
  rect?: [number, number, number, number];
  /** Free text: its text's size. */
  fontSize?: number;
  contents?: string;
  color?: [number, number, number];
  opacity?: number;
  date?: string;
}

/** A widget of a form field: where it is shown (the page as shown, points) and, for buttons, its on state. */
export interface FieldWidget {
  ref: [number, number];
  page: number | null;
  rect: [number, number, number, number];
  onState: string;
}

export type FieldType = 'text' | 'checkbox' | 'radio' | 'pushbutton' | 'combobox' | 'listbox' | 'signature';

/** A terminal field of the document's form (capi/README.md). */
export interface FieldInfo {
  ref: [number, number];
  name: string;
  label: string;
  type: FieldType;
  /** Text and combo boxes: one string (none when empty); list boxes: the chosen options; buttons: the state on ("Off"). */
  value: string[];
  options: { value: string; display: string }[];
  maxLength: number | null;
  readOnly: boolean;
  required: boolean;
  multiline: boolean;
  password: boolean;
  comb: boolean;
  editable: boolean;
  multiSelect: boolean;
  fillable: boolean;
  widgets: FieldWidget[];
}

/** An attachment of the document (capi/README.md). */
export interface AttachmentInfo {
  /** Its file specification. */
  ref: [number, number];
  name: string;
  fileName: string;
  description: string;
  mimeType: string;
  size: number | null;
  creationDate: string;
  modificationDate: string;
  relationship: string;
  /** A file attachment annotation's page; null for the document's own. */
  page: number | null;
  associated: boolean;
}

/** What capriole_document_attachment_add takes besides the bytes. */
export interface AttachmentSpec {
  fileName: string;
  description?: string;
  mimeType?: string;
  relationship?: string;
  creationDate?: string;
  modificationDate?: string;
}

/** The document's Factur-X invoice, as its XMP metadata says. */
export interface FacturxInfo {
  ref: [number, number];
  fileName: string;
  conformanceLevel: string;
  version: string;
  pdfa3: boolean;
}

/** What embedding an invoice did: pdfa3 false — the document is not PDF/A-3, so not yet a conforming Factur-X. */
export interface FacturxResult {
  ref: [number, number];
  conformanceLevel: string;
  pdfa3: boolean;
}

/** A document opened for editing: its summary, and the repairs it needed (already written into it). */
export interface EditableDocument {
  summary: Summary;
  repairs: Repair[];
}

/** capriole_status values (capriole.h). */
export const Status = {
  ok: 0,
  io: 1,
  outOfRange: 2,
  unexpectedEof: 3,
  syntax: 4,
  unsupported: 5,
  limitExceeded: 6,
  corruptData: 7,
  passwordRequired: 8,
  invalidArgument: 100,
  internal: 101,
} as const;

/** A page drawn by the engine: straight RGBA, rows top to bottom. */
export interface PageRaster {
  width: number;
  height: number;
  pixels: ArrayBuffer;
}

export type Request =
  | { kind: 'version' }
  | { kind: 'open'; data: ArrayBuffer; password: string; repair: boolean }
  | { kind: 'object'; number: number }
  | { kind: 'pageContent'; index: number; max: number }
  | { kind: 'resourceBase'; base: string }
  | { kind: 'pagePreview'; index: number }
  | { kind: 'pageRender'; index: number; dpi: number }
  | { kind: 'pageText'; index: number; order: TextOrder }
  | { kind: 'stream'; number: number; decoded: boolean }
  | { kind: 'openForEditing'; data: ArrayBuffer; password: string }
  | { kind: 'pagesRotate'; pages: number[]; degrees: number }
  | { kind: 'pagesArrange'; order: number[] }
  | { kind: 'importFile'; data: ArrayBuffer; password: string; at: number }
  | { kind: 'extract'; pages: number[] }
  | { kind: 'saveCompact'; modificationDate: string | null }
  | { kind: 'annotations'; index: number }
  | { kind: 'annotationAdd'; index: number; spec: AnnotationSpec }
  | { kind: 'annotationUpdate'; index: number; ref: [number, number]; change: AnnotationChange }
  | { kind: 'annotationRemove'; index: number; ref: [number, number] }
  | { kind: 'formFields' }
  | { kind: 'applyRedactions'; index: number }
  | { kind: 'replaceText'; index: number; area: [number, number, number, number]; text: string }
  | { kind: 'replaceParagraph'; index: number; area: [number, number, number, number]; text: string; shrink: boolean }
  | { kind: 'pkcs12Info'; p12: ArrayBuffer; password: string }
  | { kind: 'fontAdd'; data: ArrayBuffer }
  | { kind: 'fonts' }
  | { kind: 'fontsClear' }
  | { kind: 'sign'; spec: SignSpec; p12: ArrayBuffer; password: string }
  | { kind: 'signatures' }
  | { kind: 'summary' }
  | { kind: 'convertPdfa'; part: number; date: string }
  | { kind: 'attachments' }
  | { kind: 'attachmentData'; ref: [number, number] }
  | { kind: 'attachmentAdd'; spec: AttachmentSpec; data: ArrayBuffer }
  | { kind: 'attachmentRemove'; ref: [number, number] }
  | { kind: 'facturx' }
  | { kind: 'facturxEmbed'; xml: ArrayBuffer; date: string }
  | { kind: 'formFill'; ref: [number, number]; value: string | string[] | null }
  | { kind: 'save'; incremental: boolean; modificationDate: string | null }
  | { kind: 'close' };

export type Response =
  { id: number; ok: true; value: unknown } | { id: number; ok: false; status: number; message: string };
