// Graphic signatures: pictures of a handwritten signature (a scan, a photo, a drawing), put on a page as a
// stamp — a picture, not a digital signature (it proves nothing about who put it there). A few are kept
// on this device (IndexedDB, idb.ts) to be chosen at once; they never leave it.
//
// A picture given is made ready once, when added: brought within kMaxSide pixels, its white background
// made transparent when asked (a scan on white paper), its empty margins cut away, and kept as PNG.
import { RecordStore } from './idb';
import { tr } from './i18n/index.svelte';
import { graphicSignatureWords } from './i18n/graphicSignatures';

export interface GraphicSignature {
  id: string;
  /** The file's name, to tell them apart. */
  name: string;
  /** When it was added (ms since 1970): the order they are shown in. */
  added: number;
  /** The picture, ready: PNG, RGBA. */
  png: Blob;
  width: number;
  height: number;
}

/** How many are kept. */
export const maxSignatures = 4;
/** The longest side of a picture kept, in pixels: sharp at signature sizes, small to keep and to write. */
const kMaxSide = 1200;
/** Below this alpha a pixel is empty (the margins cut away). */
const kEmpty = 8;

const store = new RecordStore<GraphicSignature>('capriole-signatures', 'signatures');

/** Why a picture was refused (in the interface's language). */
export class SignatureRefused extends Error {}

function canvas(width: number, height: number): [HTMLCanvasElement, CanvasRenderingContext2D] {
  const c = document.createElement('canvas');
  c.width = width;
  c.height = height;
  const context = c.getContext('2d', { willReadFrequently: true });
  if (!context) throw new SignatureRefused(tr(graphicSignatureWords).unreadable);
  return [c, context];
}

/**
 * The picture of a file made ready: scaled within kMaxSide, white made transparent (when asked: pixels
 * fade out as they get lighter, so the ink's edges stay smooth), the empty margins cut away.
 */
export async function prepare(
  file: Blob,
  whiteToTransparent: boolean,
): Promise<{ png: Blob; width: number; height: number }> {
  const t = tr(graphicSignatureWords);
  let bitmap: ImageBitmap;
  try {
    bitmap = await createImageBitmap(file);
  } catch {
    throw new SignatureRefused(t.unreadable);
  }
  const scale = Math.min(1, kMaxSide / Math.max(bitmap.width, bitmap.height));
  const width = Math.max(1, Math.round(bitmap.width * scale));
  const height = Math.max(1, Math.round(bitmap.height * scale));
  const [, context] = canvas(width, height);
  context.drawImage(bitmap, 0, 0, width, height);
  bitmap.close();
  const image = context.getImageData(0, 0, width, height);
  const px = image.data;
  let [x0, y0, x1, y1] = [width, height, -1, -1];
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4;
      if (whiteToTransparent) {
        // Fully kept at or below 200 in its lightest-darkest channel, gone at 245 and above.
        const darkest = Math.min(px[i]!, px[i + 1]!, px[i + 2]!);
        const keep = Math.min(1, Math.max(0, (245 - darkest) / 45));
        px[i + 3] = Math.round(px[i + 3]! * keep);
      }
      if (px[i + 3]! > kEmpty) {
        x0 = Math.min(x0, x);
        y0 = Math.min(y0, y);
        x1 = Math.max(x1, x);
        y1 = Math.max(y1, y);
      }
    }
  }
  if (x1 < 0) throw new SignatureRefused(t.empty);
  const [cut, cutContext] = canvas(x1 - x0 + 1, y1 - y0 + 1);
  cutContext.putImageData(image, -x0, -y0);
  const png = await new Promise<Blob | null>((resolve) => cut.toBlob(resolve, 'image/png'));
  if (!png) throw new SignatureRefused(t.unreadable);
  return { png, width: cut.width, height: cut.height };
}

/** A signature's pixels for the engine: RGBA, row by row from the top (a canvas's ImageData). */
export async function pixelsOf(
  signature: GraphicSignature,
): Promise<{ width: number; height: number; rgba: ArrayBuffer }> {
  const bitmap = await createImageBitmap(signature.png, { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
  const [, context] = canvas(bitmap.width, bitmap.height);
  context.drawImage(bitmap, 0, 0);
  bitmap.close();
  const data = context.getImageData(0, 0, signature.width, signature.height).data;
  return { width: signature.width, height: signature.height, rgba: data.buffer.slice(0) as ArrayBuffer };
}

/** A new record's id (randomUUID needs a secure context: a site served over plain http has none). */
function uniqueId(): string {
  return typeof crypto.randomUUID === 'function'
    ? crypto.randomUUID()
    : `${Date.now()}-${Math.random().toString(36).slice(2)}`;
}

/** An attachment for an <img>: shows a picture kept, its address freed when the image goes. */
export function showPicture(png: Blob) {
  return (img: HTMLImageElement) => {
    const url = URL.createObjectURL(png);
    img.src = url;
    return () => URL.revokeObjectURL(url);
  };
}

let list = $state.raw<GraphicSignature[]>([]);
let chosen = $state<string | null>(null);
const ready = store.all().then((all) => {
  list = all.sort((a, b) => a.added - b.added);
  chosen = list[0]?.id ?? null;
});

export const graphicSignatures = {
  /** The signatures kept, oldest first (reactive). */
  get list(): GraphicSignature[] {
    return list;
  },
  /** The one the stamp tool puts (reactive); null: none kept. */
  get chosen(): GraphicSignature | null {
    return list.find((s) => s.id === chosen) ?? null;
  },
  choose(id: string): void {
    chosen = id;
  },
  /** Loaded from the device. */
  ready,
  /**
   * Adds the picture of a file, made ready, and chooses it. Throws SignatureRefused when there are
   * maxSignatures already, or the file is not a picture, or is empty; resolves to false when it could not
   * be kept on the device (it is there until the page is closed).
   */
  async add(file: File, whiteToTransparent: boolean): Promise<boolean> {
    await ready;
    if (list.length >= maxSignatures) throw new SignatureRefused(tr(graphicSignatureWords).full(maxSignatures));
    const picture = await prepare(file, whiteToTransparent);
    const signature: GraphicSignature = { id: uniqueId(), name: file.name, added: Date.now(), ...picture };
    list = [...list, signature];
    chosen = signature.id;
    return store.put(signature);
  },
  /** Forgets one. */
  async remove(id: string): Promise<void> {
    await ready;
    await store.delete(id);
    list = list.filter((s) => s.id !== id);
    if (chosen === id) chosen = list[0]?.id ?? null;
  },
};
