import { translations } from './index.svelte';

export const zoomWords = translations(
  {
    zoom: 'Zoom',
    zoomIn: 'Ingrandisci',
    zoomOut: 'Riduci',
    percent: 'Percentuale di zoom',
    fit: 'Adatta',
    fitTitle: 'La pagina intera nella finestra',
  },
  {
    en: {
      zoom: 'Zoom',
      zoomIn: 'Zoom in',
      zoomOut: 'Zoom out',
      percent: 'Zoom percentage',
      fit: 'Fit',
      fitTitle: 'The whole page in the window',
    },
    fr: {
      zoom: 'Zoom',
      zoomIn: 'Agrandir',
      zoomOut: 'Réduire',
      percent: 'Pourcentage de zoom',
      fit: 'Ajuster',
      fitTitle: 'La page entière dans la fenêtre',
    },
    de: {
      zoom: 'Zoom',
      zoomIn: 'Vergrößern',
      zoomOut: 'Verkleinern',
      percent: 'Zoomfaktor in Prozent',
      fit: 'Einpassen',
      fitTitle: 'Die ganze Seite im Fenster',
    },
    es: {
      zoom: 'Zoom',
      zoomIn: 'Ampliar',
      zoomOut: 'Reducir',
      percent: 'Porcentaje de zoom',
      fit: 'Ajustar',
      fitTitle: 'La página entera en la ventana',
    },
  },
);
