// Files for the person: sizes, dates, downloads.
import { i18n } from './i18n/index.svelte';

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

/**
 * A PDF date (D:YYYYMMDDHHmmSS...) as a date and time to read, as written (no time zone applied), in the
 * order of the interface's language ("04/10/2026 15:00" in Italian); empty when it is not one.
 */
export function readableDate(pdf: string): string {
  const m = /^D:(\d{4})(\d{2})?(\d{2})?(\d{2})?(\d{2})?/.exec(pdf);
  if (!m) return '';
  const [, y, mo = '01', d = '01', h, mi] = m;
  if (i18n.locale === 'it') return h ? `${d}/${mo}/${y} ${h}:${mi ?? '00'}` : `${d}/${mo}/${y}`;
  const date = new Date(Date.UTC(+y!, +mo - 1, +d, h ? +h : 0, mi ? +mi : 0));
  return new Intl.DateTimeFormat(i18n.intl, {
    day: '2-digit',
    month: '2-digit',
    year: 'numeric',
    ...(h ? { hour: '2-digit', minute: '2-digit' } : {}),
    timeZone: 'UTC',
  }).format(date);
}

/**
 * A PDF date as a moment, in the reader's time zone ("04/10/2026 15:00"): its offset applied (Z, +hh'mm',
 * -hh'mm'; none: taken as written). Empty when it is not one.
 */
export function localDateTime(pdf: string): string {
  const m = /^D:(\d{4})(\d{2})?(\d{2})?(\d{2})?(\d{2})?(\d{2})?([Z+-])?(\d{2})?'?(\d{2})?/.exec(pdf);
  if (!m) return '';
  const [, y, mo = '01', d = '01', h = '00', mi = '00', se = '00', sign, oh = '00', om = '00'] = m;
  if (!sign) return readableDate(pdf);
  const offset = sign === 'Z' ? 0 : (sign === '-' ? -1 : 1) * (Number(oh) * 60 + Number(om));
  const utc = Date.UTC(+y!, +mo - 1, +d, +h, +mi, +se) - offset * 60_000;
  return dateTime(new Date(utc));
}

/** A moment in the reader's time zone, as the interface's language writes it: "04/10/2026, 15:00" in Italian. */
export function dateTime(date: Date): string {
  return new Intl.DateTimeFormat(i18n.intl, {
    day: '2-digit',
    month: '2-digit',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  }).format(date);
}

/** The name without its .pdf extension. */
export function baseName(name: string): string {
  return name.replace(/\.pdf$/i, '');
}
