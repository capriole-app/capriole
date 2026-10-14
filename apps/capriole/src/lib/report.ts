// A problem report («Segnala un problema», ReportDialog): what the person wrote, and what helps to find
// the defect without saying anything of them or of their document — the versions, the browser and system
// in short, the last error, and traits of the document that carry none of its content (its PDF version,
// its pages, whether it is encrypted, its size in a range, the kinds of repair it needed). Never its
// name, its text, its metadata. The dialog shows all of it before anything is sent; the service that
// receives it (deploy/reports) keeps only these fields.

import { base } from '$app/paths';

export const reportVersion = 1;

export type SizeRange = '<1MB' | '1-10MB' | '10-100MB' | '>100MB';

export interface DocumentTraits {
  pdfVersion: string | null;
  pages: number | null;
  encrypted: boolean | null;
  size: SizeRange;
  repairs: string[];
}

export interface Report {
  v: typeof reportVersion;
  description: string;
  app: string;
  engine: string | null;
  platform: 'web';
  browser: string;
  os: string;
  language: string;
  error: string | null;
  document: DocumentTraits | null;
}

export function sizeRange(bytes: number): SizeRange {
  const mb = 1024 * 1024;
  return bytes < mb ? '<1MB' : bytes < 10 * mb ? '1-10MB' : bytes < 100 * mb ? '10-100MB' : '>100MB';
}

/** The browser and system in short («Firefox 157», «Linux»): not the whole user agent, which says more. */
export function browserOf(userAgent: string): { browser: string; os: string } {
  const major = (pattern: RegExp) => userAgent.match(pattern)?.[1];
  const edge = major(/Edg(?:e|A|iOS)?\/(\d+)/);
  const firefox = major(/(?:Firefox|FxiOS)\/(\d+)/);
  const chrome = major(/(?:Chrome|CriOS)\/(\d+)/);
  const safari = /Safari\//.test(userAgent) ? major(/Version\/(\d+)/) : undefined;
  const browser = edge
    ? `Edge ${edge}`
    : firefox
      ? `Firefox ${firefox}`
      : chrome
        ? `Chrome ${chrome}`
        : safari
          ? `Safari ${safari}`
          : 'other';
  const os = /iPhone|iPad|iPod/.test(userAgent)
    ? 'iOS'
    : /Android/.test(userAgent)
      ? 'Android'
      : /Windows/.test(userAgent)
        ? 'Windows'
        : /Mac OS X|Macintosh/.test(userAgent)
          ? 'macOS'
          : /CrOS/.test(userAgent)
            ? 'ChromeOS'
            : /Linux/.test(userAgent)
              ? 'Linux'
              : 'other';
  return { browser, os };
}

export type Sent = 'sent' | 'tooMany' | 'failed';

/** Sends a report to the service beside the app (same origin: the web app's Caddy passes it on). */
export async function send(report: Report): Promise<Sent> {
  try {
    const response = await fetch(`${base}/api/report`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(report),
    });
    if (response.status === 429) return 'tooMany';
    return response.ok ? 'sent' : 'failed';
  } catch {
    return 'failed';
  }
}
