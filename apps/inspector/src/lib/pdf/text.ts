// Showing PDF bytes to a person: strings, names, stream data.

/** The engine's JSON carries bytes as characters U+0000–U+00FF: back to bytes. */
export function latin1Bytes(text: string): Uint8Array {
  const bytes = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i) & 0xff;
  return bytes;
}

// PDFDocEncoding (ISO 32000-2 Annex D) where it differs from Latin-1: 0x18–0x1F and 0x80–0x9F.
const pdfDocHigh: Record<number, string> = {
  0x18: '˘',
  0x19: 'ˇ',
  0x1a: 'ˆ',
  0x1b: '˙',
  0x1c: '˝',
  0x1d: '˛',
  0x1e: '˚',
  0x1f: '˜',
  0x80: '•',
  0x81: '†',
  0x82: '‡',
  0x83: '…',
  0x84: '—',
  0x85: '–',
  0x86: 'ƒ',
  0x87: '⁄',
  0x88: '‹',
  0x89: '›',
  0x8a: '−',
  0x8b: '‰',
  0x8c: '„',
  0x8d: '“',
  0x8e: '”',
  0x8f: '‘',
  0x90: '’',
  0x91: '‚',
  0x92: '™',
  0x93: 'ﬁ',
  0x94: 'ﬂ',
  0x95: 'Ł',
  0x96: 'Œ',
  0x97: 'Š',
  0x98: 'Ÿ',
  0x99: 'Ž',
  0x9a: 'ı',
  0x9b: 'ł',
  0x9c: 'œ',
  0x9d: 'š',
  0x9e: 'ž',
  0xa0: '€',
};

/**
 * A text string (§7.9.2.2) as readable text: UTF-16BE or UTF-8 with their byte order mark, otherwise
 * PDFDocEncoding. null when the bytes do not look like text (binary data, e.g. an /ID).
 */
export function textOf(bytes: Uint8Array): string | null {
  if (bytes.length >= 2 && bytes[0] === 0xfe && bytes[1] === 0xff) {
    try {
      return new TextDecoder('utf-16be', { fatal: true }).decode(bytes.subarray(2));
    } catch {
      return null;
    }
  }
  if (bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) {
    try {
      return new TextDecoder('utf-8', { fatal: true }).decode(bytes.subarray(3));
    } catch {
      return null;
    }
  }
  let text = '';
  for (const byte of bytes) {
    const mapped = pdfDocHigh[byte];
    if (mapped !== undefined) text += mapped;
    else if (byte === 0x09 || byte === 0x0a || byte === 0x0d || (byte >= 0x20 && byte < 0x7f) || byte > 0xa0)
      text += String.fromCharCode(byte);
    else return null;
  }
  return text;
}

export function hexOf(bytes: Uint8Array, limit = Infinity): string {
  let out = '';
  const end = Math.min(bytes.length, limit);
  for (let i = 0; i < end; i++) out += bytes[i]!.toString(16).padStart(2, '0');
  return out;
}

/** A name as written in a file: regular characters as they are, the others as #xx (§7.3.5). */
export function nameOf(latin1: string): string {
  let out = '';
  for (const byte of latin1Bytes(latin1)) {
    const regular = byte > 0x20 && byte < 0x7f && !'()<>[]{}/%#'.includes(String.fromCharCode(byte));
    out += regular ? String.fromCharCode(byte) : `#${byte.toString(16).padStart(2, '0')}`;
  }
  return out;
}

/** Whether data reads as text (content streams, XMP, CMaps) rather than binary. */
export function looksLikeText(bytes: Uint8Array): boolean {
  const sample = bytes.subarray(0, 4096);
  if (sample.length === 0) return true;
  let binary = 0;
  for (const byte of sample) {
    if (byte === 0 || (byte < 0x20 && byte !== 0x09 && byte !== 0x0a && byte !== 0x0d)) binary++;
  }
  return binary / sample.length < 0.02;
}

/** Classic hex dump: offset, 16 bytes, their printable characters. */
export function hexDump(bytes: Uint8Array, limit: number): string {
  const lines: string[] = [];
  const end = Math.min(bytes.length, limit);
  for (let offset = 0; offset < end; offset += 16) {
    const row = bytes.subarray(offset, Math.min(offset + 16, end));
    const hex = Array.from(row, (b) => b.toString(16).padStart(2, '0')).join(' ');
    const ascii = Array.from(row, (b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : '.')).join('');
    lines.push(`${offset.toString(16).padStart(8, '0')}  ${hex.padEnd(47)}  ${ascii}`);
  }
  return lines.join('\n');
}

/** A PDF date (§7.9.4, "D:YYYYMMDDHHmmSSOHH'mm") for now, in UTC. */
export function pdfDateNow(): string {
  const d = new Date();
  const two = (n: number) => String(n).padStart(2, '0');
  return `D:${d.getUTCFullYear()}${two(d.getUTCMonth() + 1)}${two(d.getUTCDate())}${two(d.getUTCHours())}${two(d.getUTCMinutes())}${two(d.getUTCSeconds())}Z`;
}

export function formatBytes(size: number): string {
  if (size < 1024) return `${size} B`;
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`;
  return `${(size / 1024 / 1024).toFixed(1)} MB`;
}
