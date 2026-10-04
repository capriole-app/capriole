// Files for the person: sizes, dates, downloads.

export function formatBytes(size: number): string {
  if (size < 1024) return `${size} B`;
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`;
  return `${(size / 1024 / 1024).toFixed(1)} MB`;
}

/** Now as a PDF date (§7.9.4), in UTC: written as /ModDate when saving. */
export function pdfDateNow(): string {
  const d = new Date();
  const two = (n: number) => String(n).padStart(2, '0');
  return `D:${d.getUTCFullYear()}${two(d.getUTCMonth() + 1)}${two(d.getUTCDate())}${two(d.getUTCHours())}${two(d.getUTCMinutes())}${two(d.getUTCSeconds())}Z`;
}

/** Hands bytes to the browser as a download. */
export function download(data: ArrayBuffer, name: string, type = 'application/pdf'): void {
  const url = URL.createObjectURL(new Blob([data], { type }));
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/** A PDF date (D:YYYYMMDDHHmmSS...) as a date and time to read; empty when it is not one. */
export function readableDate(pdf: string): string {
  const m = /^D:(\d{4})(\d{2})?(\d{2})?(\d{2})?(\d{2})?/.exec(pdf);
  if (!m) return '';
  const [, y, mo = '01', d = '01', h, mi] = m;
  return h ? `${d}/${mo}/${y} ${h}:${mi ?? '00'}` : `${d}/${mo}/${y}`;
}

/** The name without its .pdf extension. */
export function baseName(name: string): string {
  return name.replace(/\.pdf$/i, '');
}
