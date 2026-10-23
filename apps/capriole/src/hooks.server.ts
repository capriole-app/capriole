import type { Handle } from '@sveltejs/kit';
import { pageHead } from '$lib/page-head';
import { termsKey, termsVersion } from '$lib/terms-version';

// The app is static, prerendered: this runs at build time only, and writes each page's language and head
// into app.html (src/lib/page-head.ts), with the terms' key and version its first script reads.
export const handle: Handle = ({ event, resolve }) => {
  const { lang, tags } = pageHead(event.url.pathname);
  return resolve(event, {
    transformPageChunk: ({ html }) =>
      html
        .replace('%capriole.lang%', lang)
        .replace('<!-- %capriole.head% -->', tags)
        .replace('%capriole.terms.key%', termsKey)
        .replace('%capriole.terms.version%', termsVersion),
  });
};
