import { i18n, isLocale } from '$lib/i18n/index.svelte';
import type { PageLoad } from './$types';

// One page per language for search engines (/it/, /en/…: src/hooks.server.ts writes each one's head),
// and / in English, the default (x-default). A directory each: /it/index.html. A language chosen before
// takes a person from / to its page (+layout.svelte).
export const trailingSlash = 'always';

export const load: PageLoad = ({ params }) => {
  i18n.visit(isLocale(params.lang) ? params.lang : 'en');
};
