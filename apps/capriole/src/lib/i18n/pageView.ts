import { translations } from './index.svelte';

/** The page shown large, and the panel beside it. */
export const pageViewWords = translations(
  {
    reading: 'Lettura della pagina…',
    surface: (page: number, tool: 'select' | 'form' | 'edittext' | 'draw') =>
      `Pagina ${page}: ${
        tool === 'select'
          ? 'seleziona un’annotazione o un oggetto'
          : tool === 'form'
            ? 'il modulo'
            : tool === 'edittext'
              ? 'doppio clic su una riga per modificarne il testo'
              : 'disegna'
      }`,
    annotations: 'Annotazioni',
    closeDetails: 'Chiudi i dettagli',
    objectHelp:
      'Trascinalo per spostarlo, o da un angolo per cambiarne la misura; Canc per eliminarlo. Si cambia solo l’oggetto: il resto della pagina resta com’è.',
    delete: 'Elimina',
    text: 'Testo',
    comment: 'Commento',
    commentAndColor: 'Commento e colore',
    annotationColor: "Colore dell'annotazione",
    helpParagraph:
      'Doppio clic su un paragrafo per riscriverlo: il testo va a capo dentro il paragrafo, con i suoi margini e il suo allineamento; il resto della pagina non si sposta. Gli stili (grassetto, corsivo, colori) restano sulle parole che non cambi; una parola che ne sostituisce un’altra ne prende lo stile, una aggiunta quello della parola prima. Ctrl+Invio per scrivere, Esc per lasciarlo com’era.',
    helpLine:
      'Doppio clic su una riga del testo della pagina per modificarla: Invio per scrivere, Esc per lasciarla com’era. Si cambia solo ciò che modifichi; se il font del testo non ha i caratteri nuovi, si usa un font simile.',
    helpSign:
      'Disegna il riquadro in cui comparirà la firma; poi scegli il certificato. Per una firma invisibile: Pagine › Firme.',
    helpRedact:
      'Segna ciò che va tolto: trascina sul testo, o un rettangolo altrove. Le aree segnate restano leggibili finché non applichi la redaction.',
    helpForm:
      'Compila i campi del modulo direttamente sulla pagina: il valore si scrive nel file quando lasci il campo.',
    helpSelect:
      'Clic su un’annotazione per modificarla, o su un’immagine o un disegno della pagina per spostarlo, ridimensionarlo o eliminarlo; trascina per spostare.',
    helpDraw:
      'Scegli il punto della pagina: trascina sul testo per evidenziare, sottolineare o barrare; trascina per disegnare; clic per una nota.',
    editWhat: 'Cosa si modifica',
    line: 'Riga',
    paragraph: 'Paragrafo',
    noRoom: (needed: number, fit: number) =>
      `Il testo nuovo non ci sta: servono ${needed} righe e sotto il paragrafo ce ne stanno ${fit}, senza spostare il resto della pagina.`,
    moveFollowing: 'Sposta in basso ciò che segue',
    shrink: 'Riduci il testo per farlo stare',
    backToText: 'Torna al testo',
    formFields: 'Campi del modulo',
    field: 'Campo',
    readOnly: ' (sola lettura)',
    pageAnnotations: 'Annotazioni della pagina',
    links: (n: number) => (n === 1 ? '1 link' : `${n} link`),
    none: 'Nessuna.',
    yes: 'sì',
    no: 'no',
    signatureField: 'campo firma',
    pushbutton: 'pulsante',
    names: {
      Redact: 'Da oscurare',
      Highlight: 'Evidenziazione',
      Underline: 'Sottolineatura',
      StrikeOut: 'Barrato',
      Squiggly: 'Ondulato',
      Text: 'Nota',
      Ink: 'Disegno',
      Square: 'Rettangolo',
      Circle: 'Ellisse',
      Line: 'Linea',
      FreeText: 'Testo',
      Link: 'Collegamento',
      Widget: 'Campo modulo',
      Stamp: 'Timbro',
    } as Record<string, string>,
  },
  {
    en: {
      reading: 'Reading the page…',
      surface: (page: number, tool: 'select' | 'form' | 'edittext' | 'draw') =>
        `Page ${page}: ${
          tool === 'select'
            ? 'select an annotation or an object'
            : tool === 'form'
              ? 'the form'
              : tool === 'edittext'
                ? 'double-click a line to edit its text'
                : 'draw'
        }`,
      annotations: 'Annotations',
      closeDetails: 'Close the details',
      objectHelp:
        'Drag it to move it, or by a corner to resize it; Delete removes it. Only the object changes: the rest of the page stays as it is.',
      delete: 'Delete',
      text: 'Text',
      comment: 'Comment',
      commentAndColor: 'Comment and colour',
      annotationColor: 'Colour of the annotation',
      helpParagraph:
        'Double-click a paragraph to rewrite it: the text wraps inside the paragraph, with its margins and alignment; the rest of the page does not move. Styles (bold, italic, colours) stay on the words you do not change; a word that replaces another takes its style, an added one that of the word before. Ctrl+Enter to write it, Esc to leave it as it was.',
      helpLine:
        'Double-click a line of the page’s text to edit it: Enter to write it, Esc to leave it as it was. Only what you edit changes; if the text’s font lacks the new characters, a similar font is used.',
      helpSign:
        'Draw the box where the signature will appear; then choose the certificate. For an invisible signature: Pages › Signatures.',
      helpRedact:
        'Mark what must go: drag over text, or a rectangle elsewhere. The marked areas stay readable until you apply the redaction.',
      helpForm:
        'Fill in the form’s fields right on the page: the value is written into the file when you leave the field.',
      helpSelect:
        'Click an annotation to edit it, or an image or drawing of the page to move, resize or delete it; drag to move.',
      helpDraw:
        'Choose the spot on the page: drag over text to highlight, underline or strike it out; drag to draw; click for a note.',
      editWhat: 'What is edited',
      line: 'Line',
      paragraph: 'Paragraph',
      noRoom: (needed: number, fit: number) =>
        `The new text does not fit: it needs ${needed} lines and ${fit} fit below the paragraph without moving the rest of the page.`,
      moveFollowing: 'Move what follows down',
      shrink: 'Shrink the text to fit',
      backToText: 'Back to the text',
      formFields: 'Form fields',
      field: 'Field',
      readOnly: ' (read-only)',
      pageAnnotations: 'Annotations on the page',
      links: (n: number) => (n === 1 ? '1 link' : `${n} links`),
      none: 'None.',
      yes: 'yes',
      no: 'no',
      signatureField: 'signature field',
      pushbutton: 'button',
      names: {
        Redact: 'To black out',
        Highlight: 'Highlight',
        Underline: 'Underline',
        StrikeOut: 'Strikeout',
        Squiggly: 'Squiggly',
        Text: 'Note',
        Ink: 'Drawing',
        Square: 'Rectangle',
        Circle: 'Ellipse',
        Line: 'Line',
        FreeText: 'Text',
        Link: 'Link',
        Widget: 'Form field',
        Stamp: 'Stamp',
      },
    },
    fr: {
      reading: 'Lecture de la page…',
      surface: (page: number, tool: 'select' | 'form' | 'edittext' | 'draw') =>
        `Page ${page} : ${
          tool === 'select'
            ? 'sélectionnez une annotation ou un objet'
            : tool === 'form'
              ? 'le formulaire'
              : tool === 'edittext'
                ? 'double-cliquez sur une ligne pour en modifier le texte'
                : 'dessinez'
        }`,
      annotations: 'Annotations',
      closeDetails: 'Fermer les détails',
      objectHelp:
        'Faites-le glisser pour le déplacer, ou par un coin pour le redimensionner ; Suppr pour le supprimer. Seul l’objet change : le reste de la page reste tel quel.',
      delete: 'Supprimer',
      text: 'Texte',
      comment: 'Commentaire',
      commentAndColor: 'Commentaire et couleur',
      annotationColor: 'Couleur de l’annotation',
      helpParagraph:
        'Double-cliquez sur un paragraphe pour le réécrire : le texte revient à la ligne dans le paragraphe, avec ses marges et son alignement ; le reste de la page ne bouge pas. Les styles (gras, italique, couleurs) restent sur les mots que vous ne changez pas ; un mot qui en remplace un autre prend son style, un mot ajouté celui du mot précédent. Ctrl+Entrée pour l’écrire, Échap pour le laisser tel quel.',
      helpLine:
        'Double-cliquez sur une ligne du texte de la page pour la modifier : Entrée pour l’écrire, Échap pour la laisser telle quelle. Seul ce que vous modifiez change ; si la police du texte n’a pas les nouveaux caractères, une police semblable est utilisée.',
      helpSign:
        'Tracez le cadre où la signature apparaîtra, puis choisissez le certificat. Pour une signature invisible : Pages › Signatures.',
      helpRedact:
        'Marquez ce qui doit disparaître : glissez sur le texte, ou tracez un rectangle ailleurs. Les zones marquées restent lisibles tant que vous n’appliquez pas le caviardage.',
      helpForm:
        'Remplissez les champs du formulaire directement sur la page : la valeur est écrite dans le fichier quand vous quittez le champ.',
      helpSelect:
        'Cliquez sur une annotation pour la modifier, ou sur une image ou un dessin de la page pour le déplacer, le redimensionner ou le supprimer ; glissez pour déplacer.',
      helpDraw:
        'Choisissez l’endroit sur la page : glissez sur le texte pour surligner, souligner ou barrer ; glissez pour dessiner ; cliquez pour une note.',
      editWhat: 'Ce qui est modifié',
      line: 'Ligne',
      paragraph: 'Paragraphe',
      noRoom: (needed: number, fit: number) =>
        `Le nouveau texte ne tient pas : il faut ${needed} lignes et ${fit} tiennent sous le paragraphe sans déplacer le reste de la page.`,
      moveFollowing: 'Descendre ce qui suit',
      shrink: 'Réduire le texte pour qu’il tienne',
      backToText: 'Revenir au texte',
      formFields: 'Champs du formulaire',
      field: 'Champ',
      readOnly: ' (lecture seule)',
      pageAnnotations: 'Annotations de la page',
      links: (n: number) => (n === 1 ? '1 lien' : `${n} liens`),
      none: 'Aucune.',
      yes: 'oui',
      no: 'non',
      signatureField: 'champ de signature',
      pushbutton: 'bouton',
      names: {
        Redact: 'À caviarder',
        Highlight: 'Surlignage',
        Underline: 'Soulignement',
        StrikeOut: 'Barré',
        Squiggly: 'Ondulé',
        Text: 'Note',
        Ink: 'Dessin',
        Square: 'Rectangle',
        Circle: 'Ellipse',
        Line: 'Ligne',
        FreeText: 'Texte',
        Link: 'Lien',
        Widget: 'Champ de formulaire',
        Stamp: 'Tampon',
      },
    },
    de: {
      reading: 'Seite wird gelesen…',
      surface: (page: number, tool: 'select' | 'form' | 'edittext' | 'draw') =>
        `Seite ${page}: ${
          tool === 'select'
            ? 'eine Anmerkung oder ein Objekt auswählen'
            : tool === 'form'
              ? 'das Formular'
              : tool === 'edittext'
                ? 'Doppelklick auf eine Zeile, um ihren Text zu bearbeiten'
                : 'zeichnen'
        }`,
      annotations: 'Anmerkungen',
      closeDetails: 'Details schließen',
      objectHelp:
        'Ziehen, um es zu verschieben, oder an einer Ecke, um die Größe zu ändern; Entf löscht es. Nur das Objekt ändert sich: Der Rest der Seite bleibt, wie er ist.',
      delete: 'Löschen',
      text: 'Text',
      comment: 'Kommentar',
      commentAndColor: 'Kommentar und Farbe',
      annotationColor: 'Farbe der Anmerkung',
      helpParagraph:
        'Doppelklick auf einen Absatz, um ihn neu zu schreiben: Der Text wird innerhalb des Absatzes umbrochen, mit seinen Rändern und seiner Ausrichtung; der Rest der Seite verschiebt sich nicht. Stile (fett, kursiv, Farben) bleiben auf den Wörtern, die Sie nicht ändern; ein Wort, das ein anderes ersetzt, übernimmt dessen Stil, ein hinzugefügtes den des vorherigen Wortes. Strg+Eingabe zum Schreiben, Esc lässt ihn unverändert.',
      helpLine:
        'Doppelklick auf eine Zeile des Seitentextes, um sie zu bearbeiten: Eingabe zum Schreiben, Esc lässt sie unverändert. Nur was Sie bearbeiten, ändert sich; fehlen der Schrift die neuen Zeichen, wird eine ähnliche Schrift verwendet.',
      helpSign:
        'Zeichnen Sie den Rahmen, in dem die Signatur erscheinen soll; wählen Sie dann das Zertifikat. Für eine unsichtbare Signatur: Seiten › Signaturen.',
      helpRedact:
        'Markieren Sie, was entfernt werden soll: über Text ziehen oder anderswo ein Rechteck. Die markierten Bereiche bleiben lesbar, bis Sie die Schwärzung anwenden.',
      helpForm:
        'Füllen Sie die Formularfelder direkt auf der Seite aus: Der Wert wird in die Datei geschrieben, wenn Sie das Feld verlassen.',
      helpSelect:
        'Klicken Sie auf eine Anmerkung, um sie zu bearbeiten, oder auf ein Bild oder eine Zeichnung der Seite, um das Objekt zu verschieben, seine Größe zu ändern oder es zu löschen; ziehen zum Verschieben.',
      helpDraw:
        'Wählen Sie die Stelle auf der Seite: über Text ziehen zum Hervorheben, Unterstreichen oder Durchstreichen; ziehen zum Zeichnen; klicken für eine Notiz.',
      editWhat: 'Was bearbeitet wird',
      line: 'Zeile',
      paragraph: 'Absatz',
      noRoom: (needed: number, fit: number) =>
        `Der neue Text passt nicht: Er braucht ${needed} Zeilen, und unter dem Absatz passen ${fit}, ohne den Rest der Seite zu verschieben.`,
      moveFollowing: 'Folgendes nach unten verschieben',
      shrink: 'Text verkleinern, damit er passt',
      backToText: 'Zurück zum Text',
      formFields: 'Formularfelder',
      field: 'Feld',
      readOnly: ' (schreibgeschützt)',
      pageAnnotations: 'Anmerkungen der Seite',
      links: (n: number) => (n === 1 ? '1 Link' : `${n} Links`),
      none: 'Keine.',
      yes: 'ja',
      no: 'nein',
      signatureField: 'Signaturfeld',
      pushbutton: 'Schaltfläche',
      names: {
        Redact: 'Zu schwärzen',
        Highlight: 'Hervorhebung',
        Underline: 'Unterstreichung',
        StrikeOut: 'Durchgestrichen',
        Squiggly: 'Wellenlinie',
        Text: 'Notiz',
        Ink: 'Zeichnung',
        Square: 'Rechteck',
        Circle: 'Ellipse',
        Line: 'Linie',
        FreeText: 'Text',
        Link: 'Verknüpfung',
        Widget: 'Formularfeld',
        Stamp: 'Stempel',
      },
    },
    es: {
      reading: 'Leyendo la página…',
      surface: (page: number, tool: 'select' | 'form' | 'edittext' | 'draw') =>
        `Página ${page}: ${
          tool === 'select'
            ? 'selecciona una anotación o un objeto'
            : tool === 'form'
              ? 'el formulario'
              : tool === 'edittext'
                ? 'doble clic en una línea para editar su texto'
                : 'dibuja'
        }`,
      annotations: 'Anotaciones',
      closeDetails: 'Cerrar los detalles',
      objectHelp:
        'Arrástralo para moverlo, o desde una esquina para cambiar su tamaño; Supr para eliminarlo. Solo cambia el objeto: el resto de la página se queda como está.',
      delete: 'Eliminar',
      text: 'Texto',
      comment: 'Comentario',
      commentAndColor: 'Comentario y color',
      annotationColor: 'Color de la anotación',
      helpParagraph:
        'Doble clic en un párrafo para reescribirlo: el texto se ajusta dentro del párrafo, con sus márgenes y su alineación; el resto de la página no se mueve. Los estilos (negrita, cursiva, colores) se quedan en las palabras que no cambias; una palabra que sustituye a otra toma su estilo, una añadida el de la palabra anterior. Ctrl+Intro para escribirlo, Esc para dejarlo como estaba.',
      helpLine:
        'Doble clic en una línea del texto de la página para editarla: Intro para escribirla, Esc para dejarla como estaba. Solo cambia lo que editas; si la fuente del texto no tiene los caracteres nuevos, se usa una fuente parecida.',
      helpSign:
        'Dibuja el recuadro donde aparecerá la firma; después elige el certificado. Para una firma invisible: Páginas › Firmas.',
      helpRedact:
        'Marca lo que hay que quitar: arrastra sobre el texto, o un rectángulo en otro sitio. Las áreas marcadas siguen siendo legibles hasta que apliques la censura.',
      helpForm:
        'Rellena los campos del formulario directamente en la página: el valor se escribe en el archivo cuando sales del campo.',
      helpSelect:
        'Haz clic en una anotación para editarla, o en una imagen o un dibujo de la página para moverlo, cambiar su tamaño o eliminarlo; arrastra para mover.',
      helpDraw:
        'Elige el punto de la página: arrastra sobre el texto para resaltar, subrayar o tachar; arrastra para dibujar; haz clic para una nota.',
      editWhat: 'Qué se edita',
      line: 'Línea',
      paragraph: 'Párrafo',
      noRoom: (needed: number, fit: number) =>
        `El texto nuevo no cabe: necesita ${needed} líneas y debajo del párrafo caben ${fit}, sin mover el resto de la página.`,
      moveFollowing: 'Bajar lo que sigue',
      shrink: 'Reducir el texto para que quepa',
      backToText: 'Volver al texto',
      formFields: 'Campos del formulario',
      field: 'Campo',
      readOnly: ' (solo lectura)',
      pageAnnotations: 'Anotaciones de la página',
      links: (n: number) => (n === 1 ? '1 enlace' : `${n} enlaces`),
      none: 'Ninguna.',
      yes: 'sí',
      no: 'no',
      signatureField: 'campo de firma',
      pushbutton: 'botón',
      names: {
        Redact: 'Para censurar',
        Highlight: 'Resaltado',
        Underline: 'Subrayado',
        StrikeOut: 'Tachado',
        Squiggly: 'Ondulado',
        Text: 'Nota',
        Ink: 'Dibujo',
        Square: 'Rectángulo',
        Circle: 'Elipse',
        Line: 'Línea',
        FreeText: 'Texto',
        Link: 'Enlace',
        Widget: 'Campo de formulario',
        Stamp: 'Sello',
      },
    },
  },
);
