import { translations } from './index.svelte';

// The colours offered for annotations (lib/colors.ts).

export const colorWords = translations(
  { yellow: 'Giallo', green: 'Verde', blue: 'Blu', red: 'Rosso', black: 'Nero' },
  {
    en: { yellow: 'Yellow', green: 'Green', blue: 'Blue', red: 'Red', black: 'Black' },
    fr: { yellow: 'Jaune', green: 'Vert', blue: 'Bleu', red: 'Rouge', black: 'Noir' },
    de: { yellow: 'Gelb', green: 'Grün', blue: 'Blau', red: 'Rot', black: 'Schwarz' },
    es: { yellow: 'Amarillo', green: 'Verde', blue: 'Azul', red: 'Rojo', black: 'Negro' },
  },
);
