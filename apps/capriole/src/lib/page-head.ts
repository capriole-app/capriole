import { headWords } from '$lib/i18n/head';
import { isLocale, locales, type Locale } from '$lib/i18n/index.svelte';

// The head of each page as the build writes it (src/hooks.server.ts), so that search engines read each
// language's page in its language: /it/, /en/, /fr/, /de/, /es/, each the alternate of the others
// (hreflang), and / for any other language (x-default), in English. The interface then sets the title and
// description in its own language (src/routes/+layout.svelte).

const origin = 'https://capriole.app';

/** Open Graph's locale for each language. */
const ogLocales: Record<Locale, string> = { it: 'it_IT', en: 'en_GB', fr: 'fr_FR', de: 'de_DE', es: 'es_ES' };

function escape(text: string): string {
  return text.replaceAll('&', '&amp;').replaceAll('"', '&quot;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}

/** The head's language part for the page at `path` (/ or /it/…). */
export function pageHead(path: string): { lang: Locale; tags: string } {
  const first = path.split('/')[1];
  const locale: Locale | null = isLocale(first) ? first : null;
  const lang = locale ?? 'en';
  const { title, description } = headWords[lang];
  const url = locale ? `${origin}/${locale}/` : `${origin}/`;
  const tags = [
    `<title>${escape(title)}</title>`,
    `<meta name="description" content="${escape(description)}" />`,
    `<link rel="canonical" href="${url}" />`,
    ...locales.map((l) => `<link rel="alternate" hreflang="${l}" href="${origin}/${l}/" />`),
    `<link rel="alternate" hreflang="x-default" href="${origin}/" />`,
    `<meta property="og:type" content="website" />`,
    `<meta property="og:site_name" content="Capriole" />`,
    `<meta property="og:url" content="${url}" />`,
    `<meta property="og:locale" content="${ogLocales[lang]}" />`,
    `<meta property="og:title" content="${escape(title)}" />`,
    `<meta property="og:description" content="${escape(description)}" />`,
  ];
  return { lang, tags: tags.join('\n    ') };
}
