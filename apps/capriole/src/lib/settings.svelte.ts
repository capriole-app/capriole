// The person's preferences, kept on this device: one versioned record in localStorage (the desktop
// shell's web view keeps it in the app's data, so the desktop app needs nothing else). Each value is
// checked when read, and one that is missing or wrong takes its default: a record written by another
// version, or edited by hand, never breaks the app. The language (i18n) and the theme (mode-watcher,
// read before the first paint by app.html) keep their own keys; the dialog edits them too. The graphic
// signatures are pictures, kept in IndexedDB (stamps.ts).
import { clampZoom, gridRange, pageRange } from './zoom';

export type Rgb = [number, number, number];

export interface Settings {
  /** The pages grid's zoom, in percent (also changed from the grid's own control). */
  gridZoom: number;
  /** A page shown large opens fitted to the window, or at this zoom in percent. */
  pageZoom: 'fit' | number;
  /** Written as the author of annotations and graphic signatures (/T); empty: none. */
  author: string;
  /** Highlights and notes. */
  highlightColor: Rgb;
  /** What is written and drawn: underline, strike out, pen, shapes, lines, free text. */
  drawColor: Rgb;
  /** The size of free text, in points. */
  fontSize: number;
  /** A digital signature's reason and location, filled in. */
  signReason: string;
  signLocation: string;
  /** Desktop: the smart card's PKCS#11 module, the time-stamping authority, long-term validation. */
  pkcs11Module: string;
  tsaUrl: string;
  longTerm: boolean;
}

export const defaults: Readonly<Settings> = {
  gridZoom: 100,
  pageZoom: 'fit',
  author: '',
  highlightColor: [1, 0.82, 0],
  drawColor: [0, 0, 0],
  fontSize: 12,
  signReason: '',
  signLocation: '',
  pkcs11Module: '',
  tsaUrl: '',
  longTerm: false,
};

/** The sizes free text can have (the toolbar offers these). */
export const fontSizes = [8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72];

const storageKey = 'capriole.settings';
const version = 1;
/** Kept on their own before settings existed: read once into the record, then removed. */
const legacy = { gridZoom: 'capriole.gridZoom', pkcs11Module: 'capriole.pkcs11Module', tsaUrl: 'capriole.tsaUrl' };

const text = (v: unknown, max = 200) => (typeof v === 'string' ? v.slice(0, max) : undefined);
const rgb = (v: unknown): Rgb | undefined =>
  Array.isArray(v) && v.length === 3 && v.every((c) => typeof c === 'number' && c >= 0 && c <= 1)
    ? [v[0], v[1], v[2]]
    : undefined;
const finite = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : undefined);

/** A record as read: each value checked, the wrong ones left to their default. */
export function checked(raw: unknown): Settings {
  const r = (typeof raw === 'object' && raw !== null ? raw : {}) as Record<string, unknown>;
  const grid = finite(r.gridZoom);
  const page = r.pageZoom === 'fit' ? 'fit' : finite(r.pageZoom);
  const size = finite(r.fontSize);
  return {
    gridZoom: grid !== undefined ? Math.round(clampZoom(grid, gridRange)) : defaults.gridZoom,
    pageZoom: page === 'fit' || page === undefined ? 'fit' : Math.round(clampZoom(page, pageRange)),
    author: text(r.author, 100) ?? defaults.author,
    highlightColor: rgb(r.highlightColor) ?? defaults.highlightColor,
    drawColor: rgb(r.drawColor) ?? defaults.drawColor,
    fontSize: size !== undefined && fontSizes.includes(size) ? size : defaults.fontSize,
    signReason: text(r.signReason) ?? defaults.signReason,
    signLocation: text(r.signLocation) ?? defaults.signLocation,
    pkcs11Module: text(r.pkcs11Module, 1000) ?? defaults.pkcs11Module,
    tsaUrl: text(r.tsaUrl, 1000) ?? defaults.tsaUrl,
    longTerm: typeof r.longTerm === 'boolean' ? r.longTerm : defaults.longTerm,
  };
}

function load(): Settings {
  try {
    const stored = localStorage.getItem(storageKey);
    if (stored !== null) return checked((JSON.parse(stored) as { values?: unknown }).values);
    // The first time: what was kept on its own before.
    const before: Record<string, unknown> = {};
    const grid = localStorage.getItem(legacy.gridZoom);
    if (grid !== null) before.gridZoom = Number(grid);
    before.pkcs11Module = localStorage.getItem(legacy.pkcs11Module) ?? undefined;
    before.tsaUrl = localStorage.getItem(legacy.tsaUrl) ?? undefined;
    const migrated = checked(before);
    save(migrated);
    for (const key of Object.values(legacy)) localStorage.removeItem(key);
    return migrated;
  } catch {
    // no storage, or a record that is not JSON: the defaults
    return { ...defaults };
  }
}

function save(values: Settings): void {
  try {
    localStorage.setItem(storageKey, JSON.stringify({ version, values }));
  } catch {
    // a convenience only: the settings last until the page is closed
  }
}

let current = $state<Settings>(load());

export const settings = {
  /** The preferences now (reactive). */
  get values(): Readonly<Settings> {
    return current;
  },
  /** Changes some, and keeps them. */
  update(change: Partial<Settings>): void {
    current = checked({ ...current, ...change });
    save(current);
  },
  /** Back to the defaults. */
  reset(): void {
    current = { ...defaults };
    save(current);
  },
};
