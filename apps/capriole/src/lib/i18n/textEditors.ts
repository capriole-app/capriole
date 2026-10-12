import { translations } from './index.svelte';

/** The boxes where text is written on the page: a line, a paragraph, a free text. */
export const textEditorsWords = translations(
  {
    line: 'Testo della riga',
    paragraph: 'Testo del paragrafo (Ctrl+Invio per scrivere, Esc per lasciarlo com’era)',
    textBox: 'Testo sulla pagina',
  },
  {
    en: {
      line: 'Text of the line',
      paragraph: 'Text of the paragraph (Ctrl+Enter to write it, Esc to leave it as it was)',
      textBox: 'Text on the page',
    },
    fr: {
      line: 'Texte de la ligne',
      paragraph: 'Texte du paragraphe (Ctrl+Entrée pour l’écrire, Échap pour le laisser tel quel)',
      textBox: 'Texte sur la page',
    },
    de: {
      line: 'Text der Zeile',
      paragraph: 'Text des Absatzes (Strg+Eingabe zum Schreiben, Esc lässt ihn unverändert)',
      textBox: 'Text auf der Seite',
    },
    es: {
      line: 'Texto de la línea',
      paragraph: 'Texto del párrafo (Ctrl+Intro para escribirlo, Esc para dejarlo como estaba)',
      textBox: 'Texto en la página',
    },
  },
);
