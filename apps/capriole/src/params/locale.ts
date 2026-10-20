import type { ParamMatcher } from '@sveltejs/kit';
import { isLocale } from '$lib/i18n/index.svelte';

/** A language of the interface as the first part of the path: /it/, /en/, /fr/, /de/, /es/. */
export const match = ((param: string) => isLocale(param)) satisfies ParamMatcher;
