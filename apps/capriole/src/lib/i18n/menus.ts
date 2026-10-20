import { translations } from './index.svelte';

/** The theme's and the language's menus, and the bar's «More» menu that holds them with the settings. */
export const menusWords = translations(
  {
    theme: 'Tema',
    light: 'Chiaro',
    dark: 'Scuro',
    system: 'Sistema',
    language: 'Lingua',
    more: 'Altro',
    moreTitle: 'Impostazioni, lingua e tema',
  },
  {
    en: {
      theme: 'Theme',
      light: 'Light',
      dark: 'Dark',
      system: 'System',
      language: 'Language',
      more: 'More',
      moreTitle: 'Settings, language and theme',
    },
    fr: {
      theme: 'Thème',
      light: 'Clair',
      dark: 'Sombre',
      system: 'Système',
      language: 'Langue',
      more: 'Plus',
      moreTitle: 'Paramètres, langue et thème',
    },
    de: {
      theme: 'Design',
      light: 'Hell',
      dark: 'Dunkel',
      system: 'System',
      language: 'Sprache',
      more: 'Mehr',
      moreTitle: 'Einstellungen, Sprache und Design',
    },
    es: {
      theme: 'Tema',
      light: 'Claro',
      dark: 'Oscuro',
      system: 'Sistema',
      language: 'Idioma',
      more: 'Más',
      moreTitle: 'Ajustes, idioma y tema',
    },
  },
);
