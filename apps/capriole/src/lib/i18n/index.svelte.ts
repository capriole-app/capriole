// The languages of the interface. Each area of the app keeps its words in one file of this folder, all
// languages side by side (`translations(it, { en, fr, de, es })`): Italian is the source, and the others
// must have exactly its keys — a key missing or left over fails the type check, so the gate. Words that
// depend on a number are functions. The language is the address's: /it/, /fr/… (a page per language for
// search engines), and / in English. The person's choice from the menu is remembered here, and takes them
// to its page (routes/+layout.svelte).
//
// The engine's messages (errors, repairs) are in English and stay so.

export const locales = ['it', 'en', 'fr', 'de', 'es'] as const;
export type Locale = (typeof locales)[number];

/** Each language's name, in that language (the menu shows them so). */
export const languageNames: Record<Locale, string> = {
  it: 'Italiano',
  en: 'English',
  fr: 'Français',
  de: 'Deutsch',
  es: 'Español',
};

/** The BCP 47 tag for Intl (dates, numbers). */
const intlTags: Record<Locale, string> = { it: 'it-IT', en: 'en-GB', fr: 'fr-FR', de: 'de-DE', es: 'es-ES' };

export type Translations<T> = Record<Locale, T>;

/** The words' shape without Italian's literal types: a word is any string, in any language. */
type Widen<T> = T extends string
  ? string
  : T extends (...args: infer A) => infer R
    ? (...args: A) => Widen<R>
    : T extends object
      ? { [K in keyof T]: Widen<T[K]> }
      : T;

/** An area's words: Italian, and the same keys in every other language. */
export function translations<T>(
  it: T,
  others: Record<Exclude<Locale, 'it'>, NoInfer<Widen<T>>>,
): Translations<Widen<T>> {
  return { it: it as Widen<T>, ...others };
}

const storageKey = 'capriole.locale';

export function isLocale(value: unknown): value is Locale {
  return typeof value === 'string' && (locales as readonly string[]).includes(value);
}

/** The language the person chose from the menu before, if any (kept on this device). */
export function remembered(): Locale | null {
  try {
    const stored = localStorage.getItem(storageKey);
    return isLocale(stored) ? stored : null;
  } catch {
    return null; // no storage (or the build, which has none)
  }
}

// English until the page's address says otherwise (+page.ts): / is English, /it/ Italian…
let current = $state<Locale>('en');

function apply(locale: Locale): void {
  if (typeof document !== 'undefined') document.documentElement.lang = locale;
}
apply(current);

export const i18n = {
  /** The language of the interface. */
  get locale(): Locale {
    return current;
  },
  /** The person's choice: remembered. */
  set locale(locale: Locale) {
    current = locale;
    apply(locale);
    try {
      localStorage.setItem(storageKey, locale);
    } catch {
      // a convenience only
    }
  },
  /** The language of the address (/it/, /fr/…): the page the person opened, a search result in that
   *  language, wins over a choice made before, but is not remembered as one. */
  visit(locale: Locale): void {
    current = locale;
    apply(locale);
  },
  /** The tag for Intl: dates and numbers as the language writes them. */
  get intl(): string {
    return intlTags[current];
  },
};

/** An area's words in the current language (reactive: read in a $derived or the template). */
export function tr<T>(area: Translations<T>): T {
  return area[current];
}

/** A number as the current language writes it. */
export function formatNumber(n: number, options?: Intl.NumberFormatOptions): string {
  return new Intl.NumberFormat(i18n.intl, options).format(n);
}
