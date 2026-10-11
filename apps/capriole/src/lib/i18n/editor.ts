import { translations } from './index.svelte';

// What the editor says (lib/editor.svelte.ts), the names it gives the files it saves, and the lines of a
// visible signature's appearance (written into the document: in the language of who signs). `{signer}`
// is the engine's placeholder for the signer's name and stays as it is.

export const editorWords = translations(
  {
    passwordProtected: 'Il file è protetto da password.',
    pagesSuffix: '-pagine',
    signedSuffix: '-firmato',
    signedBy: 'Firmato digitalmente da {signer}',
    date: (date: string) => `Data: ${date}`,
    reason: (reason: string) => `Motivo: ${reason}`,
    location: (location: string) => `Luogo: ${location}`,
  },
  {
    en: {
      passwordProtected: 'The file is protected by a password.',
      pagesSuffix: '-pages',
      signedSuffix: '-signed',
      signedBy: 'Digitally signed by {signer}',
      date: (date: string) => `Date: ${date}`,
      reason: (reason: string) => `Reason: ${reason}`,
      location: (location: string) => `Location: ${location}`,
    },
    fr: {
      passwordProtected: 'Le fichier est protégé par un mot de passe.',
      pagesSuffix: '-pages',
      signedSuffix: '-signé',
      signedBy: 'Signé numériquement par {signer}',
      date: (date: string) => `Date : ${date}`,
      reason: (reason: string) => `Motif : ${reason}`,
      location: (location: string) => `Lieu : ${location}`,
    },
    de: {
      passwordProtected: 'Die Datei ist durch ein Passwort geschützt.',
      pagesSuffix: '-seiten',
      signedSuffix: '-signiert',
      signedBy: 'Digital signiert von {signer}',
      date: (date: string) => `Datum: ${date}`,
      reason: (reason: string) => `Grund: ${reason}`,
      location: (location: string) => `Ort: ${location}`,
    },
    es: {
      passwordProtected: 'El archivo está protegido con contraseña.',
      pagesSuffix: '-paginas',
      signedSuffix: '-firmado',
      signedBy: 'Firmado digitalmente por {signer}',
      date: (date: string) => `Fecha: ${date}`,
      reason: (reason: string) => `Motivo: ${reason}`,
      location: (location: string) => `Lugar: ${location}`,
    },
  },
);
