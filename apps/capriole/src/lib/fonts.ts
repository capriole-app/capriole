// The fonts the person gives, remembered on this device: in the browser's IndexedDB (the desktop shell's
// web view keeps it in the app's data). They never leave it: the engine reads them in its worker, to add
// to an embedded subset the glyphs of the letters it lacks (PLAN.md phase 9c). Where there is no IndexedDB
// (a private window may refuse it) they last until the page is closed.
import type { FontFace } from '@capriole/engine-client';
import { RecordStore } from './idb';
import { tr } from './i18n/index.svelte';
import { fontWords } from './i18n/fonts';

/** A font file given: its name, its bytes, and the faces the engine read in it. */
export interface StoredFont {
  /** The file's name and size: the same file given again replaces it. */
  id: string;
  name: string;
  data: ArrayBuffer;
  faces: FontFace[];
}

const fonts = new RecordStore<StoredFont>('capriole-fonts', 'fonts');

/** The fonts remembered, in the order they were given. */
export function loadFonts(): Promise<StoredFont[]> {
  return fonts.all();
}

/** Remembers a font (replacing one of the same id); false when it could not be kept. */
export function rememberFont(font: StoredFont): Promise<boolean> {
  return fonts.put(font);
}

/** Forgets a font. */
export function forgetFont(id: string): Promise<void> {
  return fonts.delete(id);
}

/** The id of a font file: its name and size. */
export function fontId(name: string, size: number): string {
  return `${name}:${size}`;
}

/** The engine's refusal of a font file, in the interface's language (what is not recognised, as it is). */
export function fontRefusal(name: string, message: string): string {
  const t = tr(fontWords);
  if (/PostScript \(CFF\)/.test(message)) return t.cff(name);
  if (/variable font/.test(message)) return t.variable(name);
  if (/not a TrueType font|without a PostScript name/.test(message)) return t.notTrueType(name);
  return `${name}: ${message}`;
}
