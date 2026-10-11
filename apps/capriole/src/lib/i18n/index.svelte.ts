// The languages of the interface. Each area of the app keeps its words in one file of this folder, all
// languages side by side (`translations(it, { en, fr, de, es })`): Italian is the source, and the others
// must have exactly its keys — a key missing or left over fails the type check, so the gate. Words that
// depend on a number are functions. The language is the person's choice when they made one (remembered
// here), else the first of the system's languages that the app speaks, else English.
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

function isLocale(value: unknown): value is Locale {
  return typeof value === 'string' && (locales as readonly string[]).includes(value);
}

/** The language a list of BCP 47 tags (navigator.languages) asks for, among those the app speaks. */
export function preferred(tags: readonly string[]): Locale {
  for (const tag of tags) {
    const primary = tag.toLowerCase().split('-')[0];
    if (isLocale(primary)) return primary;
  }
  return 'en';
}

function initial(): Locale {
  try {
    const stored = localStorage.getItem(storageKey);
    if (isLocale(stored)) return stored;
  } catch {
    // no storage: the system's language
  }
  if (typeof navigator === 'undefined') return 'en';
  return preferred(navigator.languages?.length ? navigator.languages : [navigator.language]);
}

let current = $state<Locale>(initial());

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
