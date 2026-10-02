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
export function download(data: ArrayBuffer, name: string): void {
  const url = URL.createObjectURL(new Blob([data], { type: 'application/pdf' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/** The name without its .pdf extension. */
export function baseName(name: string): string {
  return name.replace(/\.pdf$/i, '');
}
