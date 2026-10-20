import type { Handle } from '@sveltejs/kit';
import { pageHead } from '$lib/page-head';

// The app is static, prerendered: this runs at build time only, and writes each page's language and head
// into app.html (src/lib/page-head.ts).
export const handle: Handle = ({ event, resolve }) => {
  const { lang, tags } = pageHead(event.url.pathname);
  return resolve(event, {
    transformPageChunk: ({ html }) => html.replace('%capriole.lang%', lang).replace('<!-- %capriole.head% -->', tags),
  });
};
