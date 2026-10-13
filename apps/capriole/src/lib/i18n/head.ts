import { translations } from './index.svelte';

/** The page's title and description, in the interface's language. src/app.html carries the English ones:
 *  the single static page is what search engines read, and their crawlers' browsers speak English. */
export const headWords = translations(
  {
    title: 'Capriole — editor PDF nel browser',
    description:
      'Modifica il testo, annota, compila i moduli, firma, riordina e unisci le pagine dei PDF. Tutto nel browser: i file non lasciano il tuo dispositivo.',
  },
  {
    en: {
      title: 'Capriole — PDF editor in your browser',
      description:
        'Edit text, annotate, fill in forms, sign, reorder and merge the pages of PDFs. All in your browser: your files never leave your device.',
    },
    fr: {
      title: 'Capriole — éditeur PDF dans le navigateur',
      description:
        'Modifiez le texte, annotez, remplissez les formulaires, signez, réorganisez et fusionnez les pages de vos PDF. Tout dans le navigateur : vos fichiers ne quittent pas votre appareil.',
    },
    de: {
      title: 'Capriole — PDF-Editor im Browser',
      description:
        'Text bearbeiten, kommentieren, Formulare ausfüllen, signieren, Seiten neu ordnen und zusammenführen. Alles im Browser: Ihre Dateien verlassen Ihr Gerät nicht.',
    },
    es: {
      title: 'Capriole — editor de PDF en el navegador',
      description:
        'Edita el texto, anota, rellena formularios, firma, reordena y combina las páginas de tus PDF. Todo en el navegador: tus archivos no salen de tu dispositivo.',
    },
  },
);
