// What the engine says of a conversion to PDF/A (edit/pdfa.hpp), in the language of the interface: what
// it put right, and why it refused. The engine's texts have a fixed form ("kind: count (details)", one
// reason each); what is not recognised is shown as it is.
import { tr } from './i18n/index.svelte';
import { pdfaWords } from './i18n/pdfa';

type Change = keyof (typeof pdfaWords)['it']['changes'];
type Reason = keyof (typeof pdfaWords)['it']['reasons'];

const changes: [RegExp, Change][] = [
  [/^output intent added/, 'outputIntentAdded'],
  [/^output intent kept/, 'outputIntentKept'],
  [/^fonts embedded \(substitutes/, 'fontsEmbedded'],
  [/^TrueType programs given the widths/, 'trueTypeWidths'],
  [/^widths written for embedded fonts/, 'widthsWritten'],
  [/^widths written for fonts of invisible text/, 'invisibleWidths'],
  [/^\/CIDSet removed|^\/CharSet removed/, 'fontSetsRemoved'],
  [/^TrueType CIDFonts given/, 'cidMaps'],
  [/^fonts written in place/, 'fontsInPlace'],
  [/^metadata written/, 'metadata'],
  [/^metadata of objects removed/, 'objectMetadata'],
  [/^Factur-X properties kept/, 'facturx'],
  [/^annotations removed \(hidden/, 'hiddenAnnotations'],
  [/^annotations removed \(no appearance/, 'annotationsWithoutAppearance'],
  [/^annotations made printable/, 'printable'],
  [/^annotation appearances made/, 'appearancesMade'],
  [/^appearances reduced/, 'appearancesReduced'],
  [/^actions PDF\/A forbids removed/, 'actions'],
  [/^additional actions/, 'additionalActions'],
  [/^document JavaScript removed/, 'javascript'],
  [/^\/NeedAppearances|^\/XFA|^\/NeedsRendering|^\/Requirements/, 'formSettings'],
  [/^form entries that are not fields/, 'formEntries'],
  [/^image interpolation/, 'interpolation'],
  [/^image \/(Alternates|OPI)|^form \/OPI/, 'alternates'],
  [/^transfer functions/, 'transfer'],
  [/^LZW streams/, 'lzw'],
  [/^optional content/, 'optionalContent'],
  [/^inherited resources/, 'inherited'],
  [/^page contents that are not streams/, 'contents'],
  [/^embedded files made PDF\/A-3/, 'embeddedFiles'],
];

/** A change the conversion made, translated, with its count and details. */
export function pdfaChange(line: string): string {
  const m = /^(.*?): (\d+)(?: \((.*)\))?$/.exec(line);
  const kind = m?.[1] ?? line;
  const key = changes.find(([pattern]) => pattern.test(kind))?.[1];
  if (!key) return line;
  const count = m?.[2] && m[2] !== '1' ? ` (${m[2]})` : '';
  const details = m?.[3] ? `: ${m[3]}` : '';
  return `${tr(pdfaWords).changes[key]}${count}${details}`;
}

// In order: the first that matches. The key, and the font's name for the reasons that name one.
const reasons: [RegExp, Reason][] = [
  [/document is signed/, 'signed'],
  [/encrypted/, 'encrypted'],
  [/^font (.+?) is not embedded and no font with the same metrics/, 'noSubstitute'],
  [/^font (.+?) is not embedded and its metrics are not those/, 'otherMetrics'],
  [/^font (.+?) is not embedded/, 'notEmbedded'],
  [/^font (.+?): a character it shows has no glyph in its embedded/, 'missingGlyphs'],
  [/^font (.+?): a character it shows .*substitute/, 'substituteGlyphs'],
  [/^font (.+?): the widths/, 'widths'],
  [/DeviceCMYK/, 'cmyk'],
  [/DeviceRGB/, 'rgb'],
  [/operators PDF does not define/, 'operators'],
  [/filter that is not a standard/, 'filter'],
  [/not UTF-8/, 'utf8'],
  [/embedded files/, 'attachments'],
  [/font resource that is not a font|not an object of its own/, 'fontResources'],
];

/** Why the engine refused, translated. */
export function pdfaReason(reason: string): string {
  for (const [pattern, key] of reasons) {
    const m = pattern.exec(reason);
    if (!m) continue;
    const text: string | ((font: string) => string) = tr(pdfaWords).reasons[key];
    return typeof text === 'function' ? text(m[1] ?? '') : text;
  }
  return reason;
}
