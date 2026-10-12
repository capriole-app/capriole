import { translations } from './index.svelte';

/** Graphic signatures (graphic-signatures.svelte.ts): kept in the settings, put on a page with the stamp tool. */
export const graphicSignatureWords = translations(
  {
    title: 'Firme grafiche',
    about:
      'Un’immagine della tua firma (una scansione, una foto, un disegno) da apporre sulle pagine. Non è una firma digitale: non prova chi l’ha messa e non ha il suo valore legale.',
    add: 'Aggiungi una firma…',
    whiteToTransparent: 'Rendi trasparente lo sfondo bianco',
    remove: (name: string) => `Elimina la firma «${name}»`,
    none: 'Nessuna firma salvata.',
    count: (n: number, max: number) => `${n} di ${max}`,
    full: (max: number) => `Si conservano al massimo ${max} firme: eliminane una per aggiungerne un’altra.`,
    unreadable: 'Il file non si legge come immagine (PNG, JPEG o WebP).',
    empty: 'L’immagine è vuota: senza lo sfondo bianco non resta niente.',
    notKept: 'La firma non è stata conservata sul dispositivo: resta finché la pagina è aperta.',
    choose: 'Firma da apporre',
    place: 'Clicca sulla pagina per apporre la firma, o disegna il riquadro che deve occupare.',
    manage: 'Gestisci le firme…',
  },
  {
    en: {
      title: 'Graphic signatures',
      about:
        'A picture of your signature (a scan, a photo, a drawing) to put on pages. It is not a digital signature: it does not prove who put it there and has none of its legal value.',
      add: 'Add a signature…',
      whiteToTransparent: 'Make the white background transparent',
      remove: (name: string) => `Delete the signature “${name}”`,
      none: 'No signature kept.',
      count: (n: number, max: number) => `${n} of ${max}`,
      full: (max: number) => `At most ${max} signatures are kept: delete one to add another.`,
      unreadable: 'The file cannot be read as a picture (PNG, JPEG or WebP).',
      empty: 'The picture is empty: without the white background nothing is left.',
      notKept: 'The signature could not be kept on the device: it lasts while the page is open.',
      choose: 'Signature to put',
      place: 'Click on the page to put the signature, or draw the box it should take.',
      manage: 'Manage signatures…',
    },
    fr: {
      title: 'Signatures graphiques',
      about:
        'Une image de votre signature (un scan, une photo, un dessin) à apposer sur les pages. Ce n’est pas une signature numérique : elle ne prouve pas qui l’a apposée et n’en a pas la valeur juridique.',
      add: 'Ajouter une signature…',
      whiteToTransparent: 'Rendre le fond blanc transparent',
      remove: (name: string) => `Supprimer la signature « ${name} »`,
      none: 'Aucune signature enregistrée.',
      count: (n: number, max: number) => `${n} sur ${max}`,
      full: (max: number) => `${max} signatures au plus sont conservées : supprimez-en une pour en ajouter une autre.`,
      unreadable: 'Le fichier ne se lit pas comme une image (PNG, JPEG ou WebP).',
      empty: 'L’image est vide : sans le fond blanc, il ne reste rien.',
      notKept: 'La signature n’a pas pu être conservée sur l’appareil : elle reste tant que la page est ouverte.',
      choose: 'Signature à apposer',
      place: 'Cliquez sur la page pour apposer la signature, ou tracez le cadre qu’elle doit occuper.',
      manage: 'Gérer les signatures…',
    },
    de: {
      title: 'Grafische Unterschriften',
      about:
        'Ein Bild Ihrer Unterschrift (ein Scan, ein Foto, eine Zeichnung), das auf Seiten gesetzt wird. Es ist keine digitale Signatur: Es beweist nicht, wer es gesetzt hat, und hat nicht deren Rechtswirkung.',
      add: 'Unterschrift hinzufügen…',
      whiteToTransparent: 'Weißen Hintergrund transparent machen',
      remove: (name: string) => `Unterschrift „${name}“ löschen`,
      none: 'Keine Unterschrift gespeichert.',
      count: (n: number, max: number) => `${n} von ${max}`,
      full: (max: number) =>
        `Es werden höchstens ${max} Unterschriften gespeichert: Löschen Sie eine, um eine weitere hinzuzufügen.`,
      unreadable: 'Die Datei lässt sich nicht als Bild lesen (PNG, JPEG oder WebP).',
      empty: 'Das Bild ist leer: Ohne den weißen Hintergrund bleibt nichts übrig.',
      notKept:
        'Die Unterschrift konnte nicht auf dem Gerät gespeichert werden: Sie bleibt, solange die Seite geöffnet ist.',
      choose: 'Zu setzende Unterschrift',
      place:
        'Klicken Sie auf die Seite, um die Unterschrift zu setzen, oder ziehen Sie den Rahmen, den sie einnehmen soll.',
      manage: 'Unterschriften verwalten…',
    },
    es: {
      title: 'Firmas gráficas',
      about:
        'Una imagen de tu firma (un escaneo, una foto, un dibujo) para poner en las páginas. No es una firma digital: no prueba quién la puso ni tiene su valor legal.',
      add: 'Añadir una firma…',
      whiteToTransparent: 'Hacer transparente el fondo blanco',
      remove: (name: string) => `Eliminar la firma «${name}»`,
      none: 'Ninguna firma guardada.',
      count: (n: number, max: number) => `${n} de ${max}`,
      full: (max: number) => `Se guardan como máximo ${max} firmas: elimina una para añadir otra.`,
      unreadable: 'El archivo no se lee como imagen (PNG, JPEG o WebP).',
      empty: 'La imagen está vacía: sin el fondo blanco no queda nada.',
      notKept: 'La firma no se ha podido guardar en el dispositivo: se mantiene mientras la página esté abierta.',
      choose: 'Firma que poner',
      place: 'Haz clic en la página para poner la firma, o dibuja el recuadro que debe ocupar.',
      manage: 'Gestionar las firmas…',
    },
  },
);
