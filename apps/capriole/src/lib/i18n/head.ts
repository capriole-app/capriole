import { translations } from './index.svelte';

/** The page's title and description: written into each language's page at build time, for search engines
 *  (src/lib/page-head.ts; / has the English ones), then set by the interface in its own language. The words
 *  people search for in each language (MARKETING/capriole/LANDING-brief.md, §6); the description ~155
 *  characters, what a result shows. */
export const headWords = translations(
  {
    title: 'Capriole · Modificare PDF online gratis, anche il testo',
    description:
      'Modifica il testo dei PDF, firma, compila moduli, oscura dati e unisci pagine nel browser. Gratis, senza account, senza caricare il file. Open source.',
  },
  {
    en: {
      title: 'Capriole · Edit PDFs online for free, text included',
      description:
        'Edit PDF text, sign, fill in forms, redact and merge pages in your browser. Free, no account, no upload. Open source, with apps for Windows and Linux.',
    },
    fr: {
      title: 'Capriole · Modifier un PDF en ligne gratuitement, texte compris',
      description:
        'Modifiez le texte d’un PDF, signez, remplissez des formulaires, masquez et fusionnez des pages dans le navigateur. Gratuit, sans compte, sans envoi.',
    },
    de: {
      title: 'Capriole · PDF online kostenlos bearbeiten, auch den Text',
      description:
        'PDF-Text bearbeiten, signieren, Formulare ausfüllen, schwärzen und Seiten zusammenführen, im Browser. Kostenlos, ohne Konto, ohne Upload. Open Source.',
    },
    es: {
      title: 'Capriole · Editar PDF en línea gratis, también el texto',
      description:
        'Edita el texto de un PDF, firma, rellena formularios, oculta datos y une páginas en el navegador. Gratis, sin cuenta, sin subir el archivo. Código abierto.',
    },
  },
);
