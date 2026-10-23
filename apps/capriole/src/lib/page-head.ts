import { headWords } from '$lib/i18n/head';
import { isLocale, locales, type Locale } from '$lib/i18n/index.svelte';
import { landingWords } from '$lib/i18n/landing';
import { repository } from '$lib/releases';

// The head of each page as the build writes it (src/hooks.server.ts), so that search engines read each
// language's page in its language: /it/, /en/, /fr/, /de/, /es/, each the alternate of the others
// (hreflang), and / for any other language (x-default), in English. The interface then sets the title and
// description in its own language (src/routes/+layout.svelte). With them what a link shows when shared
// (Open Graph, Twitter: the picture of each language, shots/shots.spec.ts) and what search engines read as
// data (JSON-LD: the app, and the landing's questions from the very words the page shows).

const origin = 'https://capriole.app';

/** Open Graph's locale for each language. */
const ogLocales: Record<Locale, string> = { it: 'it_IT', en: 'en_GB', fr: 'fr_FR', de: 'de_DE', es: 'es_ES' };

function escape(text: string): string {
  return text.replaceAll('&', '&amp;').replaceAll('"', '&quot;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}

/** A JSON-LD block. JSON in a script: a `<` written as its escape, so that no text can close the script. */
function jsonLd(data: object): string {
  const json = JSON.stringify({ '@context': 'https://schema.org', ...data }).replaceAll('<', '\\u003c');
  return `<script type="application/ld+json">${json}</script>`;
}

/** What the page is, as data: the application (no rating, no count of users: none that are real yet), and
 *  the landing's questions. */
function structured(lang: Locale, url: string, description: string): string[] {
  const app = {
    '@type': 'SoftwareApplication',
    name: 'Capriole',
    url,
    description,
    inLanguage: lang,
    applicationCategory: 'BusinessApplication',
    operatingSystem: 'Web, Windows, Linux',
    offers: { '@type': 'Offer', price: '0', priceCurrency: 'EUR' },
    license: 'https://opensource.org/license/0bsd',
    publisher: { '@type': 'Organization', name: 'Bobolar Ltd', email: 'hello@capriole.app' },
    sameAs: [repository],
    image: `${origin}/og/${lang}.png`,
  };
  const faq = {
    '@type': 'FAQPage',
    inLanguage: lang,
    mainEntity: landingWords[lang].faq.items.map(({ q, a }) => ({
      '@type': 'Question',
      name: q,
      acceptedAnswer: { '@type': 'Answer', text: a },
    })),
  };
  return [jsonLd(app), jsonLd(faq)];
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
    `<meta property="og:image" content="${origin}/og/${lang}.png" />`,
    `<meta property="og:image:type" content="image/png" />`,
    `<meta property="og:image:width" content="1200" />`,
    `<meta property="og:image:height" content="630" />`,
    `<meta property="og:image:alt" content="${escape(title)}" />`,
    `<meta name="twitter:card" content="summary_large_image" />`,
    ...structured(lang, url, description),
  ];
  return { lang, tags: tags.join('\n    ') };
}
