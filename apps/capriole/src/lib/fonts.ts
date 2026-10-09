// The fonts the person gives, remembered on this device: in the browser's IndexedDB (the desktop shell's
// web view keeps it in the app's data). They never leave it: the engine reads them in its worker, to add
// to an embedded subset the glyphs of the letters it lacks (PLAN.md phase 9c). Where there is no IndexedDB
// (a private window may refuse it) they last until the page is closed.
import type { FontFace } from '@capriole/engine-client';

/** A font file given: its name, its bytes, and the faces the engine read in it. */
export interface StoredFont {
  /** The file's name and size: the same file given again replaces it. */
  id: string;
  name: string;
  data: ArrayBuffer;
  faces: FontFace[];
}

const kDatabase = 'capriole-fonts';
const kStore = 'fonts';

function request<T>(r: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error ?? new Error('IndexedDB'));
  });
}

let database: Promise<IDBDatabase | null> | null = null;

/** The database, or null where there is none. */
function open(): Promise<IDBDatabase | null> {
  database ??= new Promise<IDBDatabase | null>((resolve) => {
    try {
      const r = indexedDB.open(kDatabase, 1);
      r.onupgradeneeded = () => r.result.createObjectStore(kStore, { keyPath: 'id' });
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => resolve(null);
      r.onblocked = () => resolve(null);
    } catch {
      resolve(null);
    }
  });
  return database;
}

/**
 * A write done: its transaction committed (a request's success comes before, and a page closed or reloaded
 * in between loses the write).
 */
function committed(s: IDBObjectStore): Promise<void> {
  return new Promise((resolve, reject) => {
    s.transaction.oncomplete = () => resolve();
    s.transaction.onerror = () => reject(s.transaction.error ?? new Error('IndexedDB'));
    s.transaction.onabort = () => reject(s.transaction.error ?? new Error('IndexedDB'));
  });
}

async function store(mode: IDBTransactionMode): Promise<IDBObjectStore | null> {
  const db = await open();
  return db ? db.transaction(kStore, mode).objectStore(kStore) : null;
}

/** The fonts remembered, in the order they were given. */
export async function loadFonts(): Promise<StoredFont[]> {
  try {
    const s = await store('readonly');
    return s ? await request(s.getAll() as IDBRequest<StoredFont[]>) : [];
  } catch {
    return [];
  }
}

/** Remembers a font (replacing one of the same id); false when it could not be kept. */
export async function rememberFont(font: StoredFont): Promise<boolean> {
  try {
    const s = await store('readwrite');
    if (!s) return false;
    const done = committed(s);
    await request(s.put(font));
    await done;
    return true;
  } catch {
    return false;
  }
}

/** Forgets a font. */
export async function forgetFont(id: string): Promise<void> {
  try {
    const s = await store('readwrite');
    if (s) {
      const done = committed(s);
      await request(s.delete(id));
      await done;
    }
  } catch {
    // nothing kept to forget
  }
}

/** The id of a font file: its name and size. */
export function fontId(name: string, size: number): string {
  return `${name}:${size}`;
}

/** The engine's refusal of a font file, in Italian (what is not recognised, as it is). */
export function fontRefusal(name: string, message: string): string {
  if (/PostScript \(CFF\)/.test(message))
    return `${name}: ha contorni PostScript (CFF); servono font con contorni TrueType (.ttf).`;
  if (/variable font/.test(message))
    return `${name}: è un font variabile; serve il file di uno stile (per esempio …-Regular.ttf).`;
  if (/not a TrueType font|without a PostScript name/.test(message))
    return `${name}: non è un font TrueType (.ttf, .ttc, .otf con contorni TrueType).`;
  return `${name}: ${message}`;
}
