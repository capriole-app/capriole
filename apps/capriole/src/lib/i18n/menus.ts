import { translations } from './index.svelte';

/** The theme's and the language's menus. */
export const menusWords = translations(
  { theme: 'Tema', light: 'Chiaro', dark: 'Scuro', system: 'Sistema', language: 'Lingua' },
  {
    en: { theme: 'Theme', light: 'Light', dark: 'Dark', system: 'System', language: 'Language' },
    fr: { theme: 'Thème', light: 'Clair', dark: 'Sombre', system: 'Système', language: 'Langue' },
    de: { theme: 'Design', light: 'Hell', dark: 'Dunkel', system: 'System', language: 'Sprache' },
    es: { theme: 'Tema', light: 'Claro', dark: 'Oscuro', system: 'Sistema', language: 'Idioma' },
  },
);
