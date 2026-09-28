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
  repairs: Repair[];
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
  commands: Record<'save' | 'restore' | 'clip' | 'fill' | 'stroke' | 'image' | 'shading' | 'text', number>;
  warnings: string[];
  svg: string;
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
  | { kind: 'pagePreview'; index: number }
  | { kind: 'pageRender'; index: number; dpi: number }
  | { kind: 'stream'; number: number; decoded: boolean }
  | { kind: 'save'; incremental: boolean; modificationDate: string | null }
  | { kind: 'close' };

export type Response =
  { id: number; ok: true; value: unknown } | { id: number; ok: false; status: number; message: string };
