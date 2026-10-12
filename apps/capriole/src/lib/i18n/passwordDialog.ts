import { translations } from './index.svelte';

export const passwordDialogWords = translations(
  {
    title: 'Documento protetto',
    explain: 'Il file è cifrato. Serve la password (utente o proprietario).',
    rejected: 'Password non valida.',
    password: 'Password',
    cancel: 'Annulla',
    open: 'Apri',
  },
  {
    en: {
      title: 'Protected document',
      explain: 'The file is encrypted. Its password is needed (user or owner).',
      rejected: 'Wrong password.',
      password: 'Password',
      cancel: 'Cancel',
      open: 'Open',
    },
    fr: {
      title: 'Document protégé',
      explain: 'Le fichier est chiffré. Son mot de passe est nécessaire (utilisateur ou propriétaire).',
      rejected: 'Mot de passe incorrect.',
      password: 'Mot de passe',
      cancel: 'Annuler',
      open: 'Ouvrir',
    },
    de: {
      title: 'Geschütztes Dokument',
      explain: 'Die Datei ist verschlüsselt. Das Passwort wird benötigt (Benutzer oder Eigentümer).',
      rejected: 'Falsches Passwort.',
      password: 'Passwort',
      cancel: 'Abbrechen',
      open: 'Öffnen',
    },
    es: {
      title: 'Documento protegido',
      explain: 'El archivo está cifrado. Se necesita su contraseña (de usuario o de propietario).',
      rejected: 'Contraseña incorrecta.',
      password: 'Contraseña',
      cancel: 'Cancelar',
      open: 'Abrir',
    },
  },
);
