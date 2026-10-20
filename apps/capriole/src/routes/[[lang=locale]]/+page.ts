import { i18n, isLocale } from '$lib/i18n/index.svelte';
import type { PageLoad } from './$types';

// One page per language for search engines (/it/, /en/…: src/hooks.server.ts writes each one's head),
// and / for everyone else, in the language chosen before or the system's. A directory each: /it/index.html.
export const trailingSlash = 'always';

export const load: PageLoad = ({ params }) => {
  if (isLocale(params.lang)) i18n.visit(params.lang);
};
