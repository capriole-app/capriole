// Editing a line of the page's text (the engine's capriole_document_page_replace_text, edit/text_edit.hpp):
// the line as the person reads it, what of it changed — only the characters that differ are given to the
// engine, so the rest of the line keeps its bytes and, nearly always, its font — and what the engine
// says, in Italian.
import type { PageText, ParagraphEditReport, TextEditReport } from '@capriole/engine-client';

type Box = [number, number, number, number];

/** A piece of a line's text: a character of the page (its index in chars), or a space between words (null). */
interface Piece {
  text: string;
  char: number | null;
}

/** A line of the page as it is written in its box: its text, the box, and the size of its characters. */
export interface LineText {
  index: number;
  text: string;
  box: Box;
  /** The font size the most of its characters have (points). */
  size: number;
  /** Text there is invisible (a scan's text layer): not edited. */
  invisible: boolean;
}

/** A gap between two words narrower than this many ems is no space: one word, drawn in two runs. */
const kJoined = 0.1;

function pieces(page: PageText, line: number): Piece[] {
  const [, , , , first, count] = page.lines[line]!;
  const out: Piece[] = [];
  for (let w = first; w < first + count; w++) {
    const [x0, , , , from, n] = page.words[w]!;
    if (w > first) {
      // The engine makes words in the content's order: a word's end drawn later (the text of an earlier
      // edit, at the end of the content) is a word of its own, touching the one before — no space then.
      const before = page.words[w - 1]!;
      const size = page.chars[from]?.[5] ?? 0;
      const level = page.chars[from]?.[6] === 0;
      if (!level || x0 - before[2] > kJoined * size) out.push({ text: ' ', char: null });
    }
    for (let c = from; c < from + n; c++) out.push({ text: page.chars[c]![0], char: c });
  }
  return out;
}

/** Line `line` of the page. */
export function lineText(page: PageText, line: number): LineText {
  const all = pieces(page, line);
  const chars = all.filter((p) => p.char !== null).map((p) => page.chars[p.char!]!);
  const sizes = new Map<number, number>();
  for (const c of chars) sizes.set(Math.round(c[5]), (sizes.get(Math.round(c[5])) ?? 0) + 1);
  const size = [...sizes].sort((a, b) => b[1] - a[1])[0]?.[0] ?? 12;
  const [x0, y0, x1, y1] = page.lines[line]!;
  return {
    index: line,
    text: all.map((p) => p.text).join(''),
    box: [x0, y0, x1, y1],
    size,
    invisible: chars.length > 0 && chars.every((c) => c[7] === 1),
  };
}

/** The line under a point of the page (within a couple of points of its box), or null. */
export function lineAt(page: PageText, x: number, y: number): number | null {
  const hit = page.lines.findIndex(([x0, y0, x1, y1]) => x >= x0 - 2 && x <= x1 + 2 && y >= y0 - 2 && y <= y1 + 2);
  return hit >= 0 ? hit : null;
}

/** What to ask the engine for: the characters in area become text. */
export interface Replacement {
  area: Box;
  text: string;
}

/**
 * The change from a line's text to text, as one replacement of the characters that differ (the text both
 * share at the start and at the end stays as it is); null when nothing changed. A change between
 * characters (an insertion, a space) takes the character before it along (or the one after, at the start).
 */
export function replacementFor(page: PageText, line: number, text: string): Replacement | null {
  const all = pieces(page, line);
  const old = all.map((p) => p.text).join('');
  if (text === old) return null;
  // The pieces both share at the start, then at the end (not overlapping them).
  let start = 0;
  let at = 0;
  while (start < all.length && text.startsWith(all[start]!.text, at)) {
    at += all[start]!.text.length;
    start++;
  }
  let end = all.length;
  let back = text.length;
  while (end > start && back - all[end - 1]!.text.length >= at && text.endsWith(all[end - 1]!.text, back)) {
    back -= all[end - 1]!.text.length;
    end--;
  }
  // At least one character of the page in it.
  while (!all.slice(start, end).some((p) => p.char !== null)) {
    if (start > 0) {
      start--;
      at -= all[start]!.text.length;
    } else if (end < all.length) {
      back += all[end]!.text.length;
      end++;
    } else return null; // a line with no characters
  }
  const boxes = all
    .slice(start, end)
    .filter((p) => p.char !== null)
    .map((p) => page.chars[p.char!]!);
  return {
    area: [
      Math.min(...boxes.map((c) => c[1])),
      Math.min(...boxes.map((c) => c[2])),
      Math.max(...boxes.map((c) => c[3])),
      Math.max(...boxes.map((c) => c[4])),
    ],
    text: text.slice(at, back),
  };
}

/** "al 70%", "all’85%": the article elided before a number read with a vowel (80–89, 11). */
export function toPercent(scale: number): string {
  const n = Math.round(scale * 100);
  return /^(8\d|11)$/.test(String(n)) ? `all’${n}%` : `al ${n}%`;
}

/** How the text was written, when it is worth saying: in a similar font (null: in the same font). */
export function textEditNotice(report: TextEditReport): string | null {
  if (report.method === 'sameFont') return null;
  const narrowed = report.scale < 0.995 ? `, ristretto ${toPercent(report.scale)}` : '';
  return `Il font del testo non ha tutti i caratteri nuovi: scritto con un font simile (${report.font})${narrowed}.`;
}

// ── Paragraphs ───────────────────────────────────────────────────────────────────────────────

/** A paragraph of the page (a block of its text, as laid out): its text, box, and the size of its characters. */
export interface ParagraphText {
  index: number;
  text: string;
  box: Box;
  size: number;
}

/** A word broken at the end of a line: a hyphen, then a lowercase letter starting the next. */
const kBroken = /[-\u00AD]$/;

/**
 * Block `block` of the page as one text: its lines joined by spaces — a word broken at a line's end
 * ("infor-" + "mazione") joined back.
 */
export function paragraphText(page: PageText, block: number): ParagraphText {
  const [x0, y0, x1, y1, first, count] = page.blocks[block]!;
  let text = '';
  const sizes: number[] = [];
  for (let l = first; l < first + count; l++) {
    const line = lineText(page, l);
    sizes.push(line.size);
    if (text === '') text = line.text;
    else if (kBroken.test(text) && /^\p{Ll}/u.test(line.text)) text = text.slice(0, -1) + line.text;
    else text += ' ' + line.text;
  }
  return { index: block, text, box: [x0, y0, x1, y1], size: sizes.sort((a, b) => a - b)[sizes.length >> 1] ?? 12 };
}

/** The paragraph under a point of the page (within a couple of points of its box), or null. */
export function paragraphAt(page: PageText, x: number, y: number): number | null {
  const hit = page.blocks.findIndex(([x0, y0, x1, y1]) => x >= x0 - 2 && x <= x1 + 2 && y >= y0 - 2 && y <= y1 + 2);
  return hit >= 0 ? hit : null;
}

/** The area to give the engine for a paragraph: its box, a third of a point around. */
export function paragraphArea(paragraph: ParagraphText): Box {
  const [x0, y0, x1, y1] = paragraph.box;
  return [x0 - 0.3, y0 - 0.3, x1 + 0.3, y1 + 0.3];
}

/** How a paragraph was written, when it is worth saying: in a similar font, or smaller (null: neither). */
export function paragraphNotice(report: ParagraphEditReport): string | null {
  const parts: string[] = [];
  if (report.method === 'similarFont')
    parts.push(
      report.styles > 1 || report.sameFontStyles === 0
        ? 'i caratteri che i font del paragrafo non hanno sono scritti con un font simile'
        : `il font del paragrafo non ha tutti i caratteri nuovi: scritto con un font simile (${report.font})`,
    );
  if (report.scale < 0.995) parts.push(`ridotto ${toPercent(report.scale)} per farlo stare`);
  if (parts.length === 0) return null;
  const text = parts.join('; ');
  return text.charAt(0).toUpperCase() + text.slice(1) + '.';
}

/** "no room: the new text needs N lines, M fit": the numbers, or null for another refusal. */
export function noRoom(message: string): { needed: number; fit: number } | null {
  const m = /no room: the new text needs (\d+) lines, (\d+) fit/.exec(message);
  return m ? { needed: Number(m[1]), fit: Number(m[2]) } : null;
}

const refusals: [RegExp, string][] = [
  [/several lines|one line at a time: the area/, 'Si modifica una riga alla volta.'],
  [/one paragraph at a time/, 'Si modifica un paragrafo alla volta: qui le righe sono di più paragrafi.'],
  [
    /superscripts or subscripts/,
    'Il paragrafo ha apici o pedici, che il testo semplice non sa dire: modificalo riga per riga.',
  ],
  [/only horizontal/, 'Si reimpaginano solo i paragrafi orizzontali: modificalo riga per riga.'],
  [/line break/, 'Il testo nuovo deve stare su una riga.'],
  [/invisible/, 'Il testo qui è invisibile (lo strato di testo di una scansione): non si modifica.'],
  [/annotation's/, 'Questo testo appartiene a un’annotazione o a un campo del modulo: si modifica lì.'],
  [
    /standard fonts cannot write/,
    'Alcuni caratteri non si possono scrivere né con il font del testo né con uno simile.',
  ],
  [/no text in the area/, 'Qui non c’è testo da modificare.'],
  [/no room/, 'Il testo nuovo non ci sta nel paragrafo.'],
];

/** The engine's refusal of an edit, in Italian (what is not recognised, as it is). */
export function textEditRefusal(message: string): string {
  return refusals.find(([pattern]) => pattern.test(message))?.[1] ?? message;
}
