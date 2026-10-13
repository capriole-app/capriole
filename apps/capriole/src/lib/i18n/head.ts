import { translations } from './index.svelte';

/** The page's title and description, in the interface's language. src/app.html carries the English ones:
 *  the single static page is what search engines read, and their crawlers' browsers speak English. */
export const headWords = translations(
  {
    title: 'Capriole — editor PDF nel browser',
    description:
      'Modifica i PDF nel browser: testo e immagini su ogni pagina, firme, moduli, unione e divisione. I tuoi file non lasciano mai il dispositivo.',
  },
  {
    en: {
      title: 'Capriole — PDF editor in your browser',
      description:
        'Edit PDFs in your browser: text and images on every page, signatures, forms, merging and splitting. Your files never leave your device.',
    },
    fr: {
      title: 'Capriole — éditeur PDF dans le navigateur',
      description:
        'Modifiez vos PDF dans le navigateur : texte et images sur chaque page, signatures, formulaires, fusion et découpage. Vos fichiers restent chez vous.',
    },
    de: {
      title: 'Capriole — PDF-Editor im Browser',
      description:
        'PDFs im Browser bearbeiten: Text und Bilder auf jeder Seite, Signaturen, Formulare, Zusammenführen und Teilen. Ihre Dateien bleiben auf Ihrem Gerät.',
    },
    es: {
      title: 'Capriole — editor de PDF en el navegador',
      description:
        'Edita PDF en el navegador: texto e imágenes en cada página, firmas, formularios, unir y dividir. Tus archivos no salen de tu dispositivo.',
    },
  },
);
