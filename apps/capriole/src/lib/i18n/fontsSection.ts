import { translations } from './index.svelte';

export const fontsSectionWords = translations(
  {
    add: 'Aggiungi font…',
    title: 'Font forniti',
    explain:
      'Se il font incorporato nel documento non ha una lettera, la si prende dallo stesso font fornito qui (la stessa versione), se la sua licenza lo consente. I font restano su questo dispositivo.',
    notEmbeddable: ' — la licenza non consente di aggiungerne lettere',
    remove: (name: string) => `Rimuovi ${name}`,
  },
  {
    en: {
      add: 'Add fonts…',
      title: 'Fonts provided',
      explain:
        'When a font embedded in the document lacks a letter, it is taken from the same font provided here (the same version), if its licence allows it. The fonts stay on this device.',
      notEmbeddable: ' — its licence does not allow adding letters',
      remove: (name: string) => `Remove ${name}`,
    },
    fr: {
      add: 'Ajouter des polices…',
      title: 'Polices fournies',
      explain:
        'Si une police incorporée au document n’a pas une lettre, celle-ci est prise dans la même police fournie ici (la même version), si sa licence le permet. Les polices restent sur cet appareil.',
      notEmbeddable: ' — sa licence ne permet pas d’y ajouter des lettres',
      remove: (name: string) => `Retirer ${name}`,
    },
    de: {
      add: 'Schriften hinzufügen…',
      title: 'Bereitgestellte Schriften',
      explain:
        'Fehlt einer im Dokument eingebetteten Schrift ein Buchstabe, wird er aus derselben hier bereitgestellten Schrift (derselben Version) genommen, sofern ihre Lizenz es erlaubt. Die Schriften bleiben auf diesem Gerät.',
      notEmbeddable: ' — die Lizenz erlaubt nicht, Buchstaben hinzuzufügen',
      remove: (name: string) => `${name} entfernen`,
    },
    es: {
      add: 'Añadir fuentes…',
      title: 'Fuentes proporcionadas',
      explain:
        'Si una fuente incrustada en el documento no tiene una letra, se toma de la misma fuente proporcionada aquí (la misma versión), si su licencia lo permite. Las fuentes se quedan en este dispositivo.',
      notEmbeddable: ' — su licencia no permite añadirle letras',
      remove: (name: string) => `Quitar ${name}`,
    },
  },
);
