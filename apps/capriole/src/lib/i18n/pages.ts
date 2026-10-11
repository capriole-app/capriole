import { translations } from './index.svelte';

/** The grid of pages, its thumbnails, and the page around them. */
export const pagesWords = translations(
  {
    grid: 'Pagine del documento',
    page: (n: number) => `Pagina ${n}`,
    doubleClick: 'Doppio clic per annotare',
    notDrawn: 'non disegnata',
    repaired: (n: number) =>
      `Il file era danneggiato: il motore lo ha riparato (${n} ${n === 1 ? 'riparazione' : 'riparazioni'}). Salvando, si salva la versione riparata.`,
  },
  {
    en: {
      grid: 'Pages of the document',
      page: (n: number) => `Page ${n}`,
      doubleClick: 'Double-click to annotate',
      notDrawn: 'not drawn',
      repaired: (n: number) =>
        `The file was damaged: the engine repaired it (${n} ${n === 1 ? 'repair' : 'repairs'}). Saving saves the repaired version.`,
    },
    fr: {
      grid: 'Pages du document',
      page: (n: number) => `Page ${n}`,
      doubleClick: 'Double-cliquez pour annoter',
      notDrawn: 'non affichée',
      repaired: (n: number) =>
        `Le fichier était endommagé : le moteur l’a réparé (${n} ${n === 1 ? 'réparation' : 'réparations'}). En enregistrant, c’est la version réparée qui est enregistrée.`,
    },
    de: {
      grid: 'Seiten des Dokuments',
      page: (n: number) => `Seite ${n}`,
      doubleClick: 'Doppelklick zum Annotieren',
      notDrawn: 'nicht dargestellt',
      repaired: (n: number) =>
        `Die Datei war beschädigt: Die Engine hat sie repariert (${n} ${n === 1 ? 'Reparatur' : 'Reparaturen'}). Beim Speichern wird die reparierte Fassung gespeichert.`,
    },
    es: {
      grid: 'Páginas del documento',
      page: (n: number) => `Página ${n}`,
      doubleClick: 'Doble clic para anotar',
      notDrawn: 'no dibujada',
      repaired: (n: number) =>
        `El archivo estaba dañado: el motor lo ha reparado (${n} ${n === 1 ? 'reparación' : 'reparaciones'}). Al guardar, se guarda la versión reparada.`,
    },
  },
);
