import { translations } from './index.svelte';

// The fonts given (lib/fonts.ts, editor.addFonts): why one was refused, or could not be remembered.

export const fontWords = translations(
  {
    cff: (name: string) => `${name}: ha contorni PostScript (CFF); servono font con contorni TrueType (.ttf).`,
    variable: (name: string) => `${name}: è un font variabile; serve il file di uno stile (per esempio …-Regular.ttf).`,
    notTrueType: (name: string) => `${name}: non è un font TrueType (.ttf, .ttc, .otf con contorni TrueType).`,
    notKept: 'Il browser non lascia ricordare i font su questo dispositivo: restano finché la pagina è aperta.',
  },
  {
    en: {
      cff: (name: string) =>
        `${name}: it has PostScript (CFF) outlines; fonts with TrueType outlines (.ttf) are needed.`,
      variable: (name: string) =>
        `${name}: it is a variable font; the file of one style is needed (for example …-Regular.ttf).`,
      notTrueType: (name: string) => `${name}: it is not a TrueType font (.ttf, .ttc, .otf with TrueType outlines).`,
      notKept: "The browser doesn't let fonts be remembered on this device: they last while the page is open.",
    },
    fr: {
      cff: (name: string) => `${name} : contours PostScript (CFF) ; il faut des polices à contours TrueType (.ttf).`,
      variable: (name: string) =>
        `${name} : c’est une police variable ; il faut le fichier d’un style (par exemple …-Regular.ttf).`,
      notTrueType: (name: string) =>
        `${name} : ce n’est pas une police TrueType (.ttf, .ttc, .otf à contours TrueType).`,
      notKept:
        'Le navigateur ne permet pas de mémoriser les polices sur cet appareil : elles restent tant que la page est ouverte.',
    },
    de: {
      cff: (name: string) =>
        `${name}: hat PostScript-Konturen (CFF); benötigt werden Schriften mit TrueType-Konturen (.ttf).`,
      variable: (name: string) =>
        `${name}: ist eine variable Schrift; benötigt wird die Datei eines Schriftschnitts (zum Beispiel …-Regular.ttf).`,
      notTrueType: (name: string) => `${name}: ist keine TrueType-Schrift (.ttf, .ttc, .otf mit TrueType-Konturen).`,
      notKept:
        'Der Browser erlaubt nicht, Schriften auf diesem Gerät zu speichern: Die Schriften bleiben nur erhalten, solange die Seite geöffnet ist.',
    },
    es: {
      cff: (name: string) =>
        `${name}: tiene contornos PostScript (CFF); se necesitan fuentes con contornos TrueType (.ttf).`,
      variable: (name: string) =>
        `${name}: es una fuente variable; se necesita el archivo de un estilo (por ejemplo …-Regular.ttf).`,
      notTrueType: (name: string) => `${name}: no es una fuente TrueType (.ttf, .ttc, .otf con contornos TrueType).`,
      notKept:
        'El navegador no permite recordar las fuentes en este dispositivo: se mantienen mientras la página esté abierta.',
    },
  },
);
