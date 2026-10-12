import { translations } from './index.svelte';

// What an edit of the page's text says (lib/textedit.ts): how it was written, and the engine's refusals.
// `percent` is a whole number (70 for 70%); `points` is already written as the language writes numbers.

export const textEditWords = translations(
  {
    /** "al 70%", "all’85%": the article elided before a number read with a vowel (80–89, 11). */
    toPercent: (n: number) => (/^(8\d|11)$/.test(String(n)) ? `all’${n}%` : `al ${n}%`),
    letters: (n: number) => (n === 1 ? '1 lettera' : `${n} lettere`),
    added: (added: number, letters: string, font: string | null) =>
      `${added === 1 ? 'Aggiunta' : 'Aggiunte'} ${letters} ${font ? `al font incorporato (${font})` : 'ai font incorporati'} dal font che hai fornito.`,
    narrowed: (percent: string) => `, ristretto ${percent}`,
    similarFont: (font: string, narrowed: string) =>
      `Il font del testo non ha tutti i caratteri nuovi: scritto con un font simile (${font})${narrowed}.`,
    missingFont: (names: string) =>
      `Per scriverli nel font del documento, aggiungi il font ${names} (il suo file .ttf) e rifai la modifica.`,
    missingFonts: (names: string) =>
      `Per scriverli nei font del documento, aggiungi i font ${names} (i loro file .ttf) e rifai la modifica.`,
    paragraphSimilarFonts: 'i caratteri che i font del paragrafo non hanno sono scritti con un font simile',
    paragraphSimilarFont: (font: string) =>
      `il font del paragrafo non ha tutti i caratteri nuovi: scritto con un font simile (${font})`,
    reduced: (percent: string) => `ridotto ${percent} per farlo stare`,
    items: (n: string, one: boolean) => `${n} ${one ? 'elemento' : 'elementi'}`,
    annotations: (n: string, one: boolean) => `${n} ${one ? 'annotazione' : 'annotazioni'}`,
    and: ' e ',
    moved: (points: string, what: string) => `ciò che segue è sceso di ${points} pt per fargli spazio (${what})`,
    notMoved: 'Ciò che segue non si può spostare in basso:',
    wouldReachFooter: 'arriverebbe al piè di pagina',
    wouldPassBottom: 'andrebbe oltre il fondo della pagina',
    needsPoints: (needed: string, free: string) =>
      `(servono ${needed} pt, ce ne sono ${free} liberi). Puoi ridurre il testo per farlo stare.`,
    insideBox: 'il paragrafo è dentro un riquadro (una cornice, una cella) che non si allunga con lui.',
    runInto: 'urterebbe ciò che gli sta accanto.',
    imageAcross: 'sotto il paragrafo passa un’immagine (una pagina scansionata, lo sfondo di un modulo).',
    drawingAcross: 'il fondo del paragrafo è attraversato da un disegno (un riquadro, una tabella, una riga).',
    annotationAcross: 'il fondo del paragrafo è attraversato da un’annotazione.',
    nothingToMove: 'Sotto il paragrafo non c’è niente da spostare.',
    cannotMove: 'qui la pagina è disegnata in un modo che non si sa spostare.',
    oneLine: 'Si modifica una riga alla volta.',
    oneParagraph: 'Si modifica un paragrafo alla volta: qui le righe sono di più paragrafi.',
    superscripts: 'Il paragrafo ha apici o pedici, che il testo semplice non sa dire: modificalo riga per riga.',
    onlyHorizontal: 'Si reimpaginano solo i paragrafi orizzontali: modificalo riga per riga.',
    lineBreak: 'Il testo nuovo deve stare su una riga.',
    invisible: 'Il testo qui è invisibile (lo strato di testo di una scansione): non si modifica.',
    annotationText: 'Questo testo appartiene a un’annotazione o a un campo del modulo: si modifica lì.',
    standardFonts: 'Alcuni caratteri non si possono scrivere né con il font del testo né con uno simile.',
    noText: 'Qui non c’è testo da modificare.',
    noRoom: 'Il testo nuovo non ci sta nel paragrafo.',
  },
  {
    en: {
      toPercent: (n: number) => `to ${n}%`,
      letters: (n: number) => (n === 1 ? '1 letter' : `${n} letters`),
      added: (_added: number, letters: string, font: string | null) =>
        `Added ${letters} to ${font ? `the embedded font (${font})` : 'the embedded fonts'} from the font you provided.`,
      narrowed: (percent: string) => `, narrowed ${percent}`,
      similarFont: (font: string, narrowed: string) =>
        `The text's font lacks some of the new characters: written with a similar font (${font})${narrowed}.`,
      missingFont: (names: string) =>
        `To write them in the document's font, add the font ${names} (its .ttf file) and make the edit again.`,
      missingFonts: (names: string) =>
        `To write them in the document's fonts, add the fonts ${names} (their .ttf files) and make the edit again.`,
      paragraphSimilarFonts: "the characters the paragraph's fonts lack are written with a similar font",
      paragraphSimilarFont: (font: string) =>
        `the paragraph's font lacks some of the new characters: written with a similar font (${font})`,
      reduced: (percent: string) => `reduced ${percent} to fit`,
      items: (n: string, one: boolean) => `${n} ${one ? 'item' : 'items'}`,
      annotations: (n: string, one: boolean) => `${n} ${one ? 'annotation' : 'annotations'}`,
      and: ' and ',
      moved: (points: string, what: string) => `what follows moved down ${points} pt to make room (${what})`,
      notMoved: 'What follows cannot be moved down:',
      wouldReachFooter: 'it would reach the footer',
      wouldPassBottom: 'it would go past the bottom of the page',
      needsPoints: (needed: string, free: string) =>
        `(it needs ${needed} pt, ${free} are free). You can shorten the text to make it fit.`,
      insideBox: "the paragraph is inside a box (a frame, a cell) that doesn't grow with it.",
      runInto: 'it would run into what is beside it.',
      imageAcross: 'an image lies under the paragraph (a scanned page, the background of a form).',
      drawingAcross: "a drawing runs across the paragraph's bottom (a box, a table, a rule).",
      annotationAcross: "an annotation lies across the paragraph's bottom.",
      nothingToMove: 'There is nothing below the paragraph to move.',
      cannotMove: 'the page is drawn here in a way that cannot be moved.',
      oneLine: 'One line is edited at a time.',
      oneParagraph: 'One paragraph is edited at a time: these lines belong to several paragraphs.',
      superscripts:
        'The paragraph has superscripts or subscripts, which plain text cannot express: edit it line by line.',
      onlyHorizontal: 'Only horizontal paragraphs can be laid out again: edit it line by line.',
      lineBreak: 'The new text must fit on one line.',
      invisible: "The text here is invisible (a scan's text layer): it can't be edited.",
      annotationText: 'This text belongs to an annotation or a form field: edit it there.',
      standardFonts: "Some characters can't be written with the text's font or with a similar one.",
      noText: 'There is no text to edit here.',
      noRoom: "The new text doesn't fit in the paragraph.",
    },
    fr: {
      toPercent: (n: number) => `à ${n} %`,
      letters: (n: number) => (n === 1 ? '1 caractère' : `${n} caractères`),
      added: (added: number, letters: string, font: string | null) =>
        `${letters} ${added === 1 ? 'ajouté' : 'ajoutés'} ${font ? `à la police incorporée (${font})` : 'aux polices incorporées'} depuis la police que vous avez fournie.`,
      narrowed: (percent: string) => `, resserré ${percent}`,
      similarFont: (font: string, narrowed: string) =>
        `La police du texte n’a pas tous les nouveaux caractères : écrit avec une police semblable (${font})${narrowed}.`,
      missingFont: (names: string) =>
        `Pour les écrire dans la police du document, ajoutez la police ${names} (son fichier .ttf) et refaites la modification.`,
      missingFonts: (names: string) =>
        `Pour les écrire dans les polices du document, ajoutez les polices ${names} (leurs fichiers .ttf) et refaites la modification.`,
      paragraphSimilarFonts: 'les caractères absents des polices du paragraphe sont écrits avec une police semblable',
      paragraphSimilarFont: (font: string) =>
        `la police du paragraphe n’a pas tous les nouveaux caractères : écrit avec une police semblable (${font})`,
      reduced: (percent: string) => `réduit ${percent} pour qu’il tienne`,
      items: (n: string, one: boolean) => `${n} ${one ? 'élément' : 'éléments'}`,
      annotations: (n: string, one: boolean) => `${n} ${one ? 'annotation' : 'annotations'}`,
      and: ' et ',
      moved: (points: string, what: string) =>
        `la suite est descendue de ${points} pt pour lui faire de la place (${what})`,
      notMoved: 'La suite ne peut pas être déplacée vers le bas :',
      wouldReachFooter: 'elle atteindrait le pied de page',
      wouldPassBottom: 'elle dépasserait le bas de la page',
      needsPoints: (needed: string, free: string) =>
        `(il faut ${needed} pt, ${free} sont libres). Vous pouvez raccourcir le texte pour qu’il tienne.`,
      insideBox: 'le paragraphe est dans un cadre (une bordure, une cellule) qui ne s’agrandit pas avec lui.',
      runInto: 'elle heurterait ce qui se trouve à côté.',
      imageAcross: 'une image passe sous le paragraphe (une page numérisée, le fond d’un formulaire).',
      drawingAcross: 'un dessin traverse le bas du paragraphe (un cadre, un tableau, un filet).',
      annotationAcross: 'une annotation traverse le bas du paragraphe.',
      nothingToMove: 'Il n’y a rien à déplacer sous le paragraphe.',
      cannotMove: 'ici la page est dessinée d’une manière qui ne peut pas être déplacée.',
      oneLine: 'On modifie une ligne à la fois.',
      oneParagraph: 'On modifie un paragraphe à la fois : ces lignes appartiennent à plusieurs paragraphes.',
      superscripts:
        'Le paragraphe contient des exposants ou des indices, que le texte simple ne sait pas exprimer : modifiez-le ligne par ligne.',
      onlyHorizontal: 'Seuls les paragraphes horizontaux peuvent être remis en page : modifiez-le ligne par ligne.',
      lineBreak: 'Le nouveau texte doit tenir sur une ligne.',
      invisible: 'Le texte ici est invisible (la couche de texte d’une numérisation) : il ne se modifie pas.',
      annotationText: 'Ce texte appartient à une annotation ou à un champ du formulaire : modifiez-le là.',
      standardFonts:
        'Certains caractères ne peuvent être écrits ni avec la police du texte ni avec une police semblable.',
      noText: 'Il n’y a pas de texte à modifier ici.',
      noRoom: 'Le nouveau texte ne tient pas dans le paragraphe.',
    },
    de: {
      toPercent: (n: number) => `auf ${n} %`,
      letters: (n: number) => (n === 1 ? '1 Zeichen' : `${n} Zeichen`),
      added: (_added: number, letters: string, font: string | null) =>
        `${letters} aus der von Ihnen bereitgestellten Schrift ${font ? `zur eingebetteten Schrift (${font})` : 'zu den eingebetteten Schriften'} hinzugefügt.`,
      narrowed: (percent: string) => `, ${percent} verschmälert`,
      similarFont: (font: string, narrowed: string) =>
        `Der Schrift des Textes fehlen einige der neuen Zeichen: mit einer ähnlichen Schrift geschrieben (${font})${narrowed}.`,
      missingFont: (names: string) =>
        `Um sie in der Schrift des Dokuments zu schreiben, fügen Sie die Schrift ${names} (ihre .ttf-Datei) hinzu und wiederholen Sie die Änderung.`,
      missingFonts: (names: string) =>
        `Um sie in den Schriften des Dokuments zu schreiben, fügen Sie die Schriften ${names} (ihre .ttf-Dateien) hinzu und wiederholen Sie die Änderung.`,
      paragraphSimilarFonts:
        'die Zeichen, die den Schriften des Absatzes fehlen, sind mit einer ähnlichen Schrift geschrieben',
      paragraphSimilarFont: (font: string) =>
        `der Schrift des Absatzes fehlen einige der neuen Zeichen: mit einer ähnlichen Schrift geschrieben (${font})`,
      reduced: (percent: string) => `${percent} verkleinert, damit er passt`,
      items: (n: string, one: boolean) => `${n} ${one ? 'Element' : 'Elemente'}`,
      annotations: (n: string, one: boolean) => `${n} ${one ? 'Anmerkung' : 'Anmerkungen'}`,
      and: ' und ',
      moved: (points: string, what: string) =>
        `das Folgende wurde um ${points} pt nach unten verschoben, um Platz zu schaffen (${what})`,
      notMoved: 'Das Folgende lässt sich nicht nach unten verschieben:',
      wouldReachFooter: 'Es würde die Fußzeile erreichen',
      wouldPassBottom: 'Es würde über den unteren Rand der Seite hinausgehen',
      needsPoints: (needed: string, free: string) =>
        `(benötigt werden ${needed} pt, frei sind ${free}). Sie können den Text kürzen, damit er passt.`,
      insideBox: 'Der Absatz steht in einem Rahmen (einer Umrandung, einer Zelle), der nicht mit ihm wächst.',
      runInto: 'Es würde mit dem daneben Stehenden zusammenstoßen.',
      imageAcross: 'Unter dem Absatz liegt ein Bild (eine gescannte Seite, der Hintergrund eines Formulars).',
      drawingAcross: 'Eine Zeichnung verläuft über das Ende des Absatzes (ein Rahmen, eine Tabelle, eine Linie).',
      annotationAcross: 'Eine Anmerkung liegt über dem Ende des Absatzes.',
      nothingToMove: 'Unter dem Absatz gibt es nichts zu verschieben.',
      cannotMove: 'Die Seite ist hier auf eine Weise gezeichnet, die sich nicht verschieben lässt.',
      oneLine: 'Es wird jeweils eine Zeile bearbeitet.',
      oneParagraph: 'Es wird jeweils ein Absatz bearbeitet: Diese Zeilen gehören zu mehreren Absätzen.',
      superscripts:
        'Der Absatz enthält Hoch- oder Tiefstellungen, die reiner Text nicht wiedergeben kann: Bearbeiten Sie ihn Zeile für Zeile.',
      onlyHorizontal: 'Nur waagerechte Absätze lassen sich neu umbrechen: Bearbeiten Sie ihn Zeile für Zeile.',
      lineBreak: 'Der neue Text muss in eine Zeile passen.',
      invisible: 'Der Text hier ist unsichtbar (die Textebene eines Scans): Er lässt sich nicht bearbeiten.',
      annotationText: 'Dieser Text gehört zu einer Anmerkung oder einem Formularfeld: Bearbeiten Sie ihn dort.',
      standardFonts: 'Einige Zeichen lassen sich weder mit der Schrift des Textes noch mit einer ähnlichen schreiben.',
      noText: 'Hier gibt es keinen Text zu bearbeiten.',
      noRoom: 'Der neue Text passt nicht in den Absatz.',
    },
    es: {
      toPercent: (n: number) => `al ${n} %`,
      letters: (n: number) => (n === 1 ? '1 carácter' : `${n} caracteres`),
      added: (added: number, letters: string, font: string | null) =>
        `${added === 1 ? 'Añadido' : 'Añadidos'} ${letters} ${font ? `a la fuente incrustada (${font})` : 'a las fuentes incrustadas'} desde la fuente que has proporcionado.`,
      narrowed: (percent: string) => `, estrechado ${percent}`,
      similarFont: (font: string, narrowed: string) =>
        `A la fuente del texto le faltan algunos de los caracteres nuevos: escrito con una fuente parecida (${font})${narrowed}.`,
      missingFont: (names: string) =>
        `Para escribirlos con la fuente del documento, añade la fuente ${names} (su archivo .ttf) y vuelve a hacer el cambio.`,
      missingFonts: (names: string) =>
        `Para escribirlos con las fuentes del documento, añade las fuentes ${names} (sus archivos .ttf) y vuelve a hacer el cambio.`,
      paragraphSimilarFonts: 'los caracteres que faltan en las fuentes del párrafo se escriben con una fuente parecida',
      paragraphSimilarFont: (font: string) =>
        `a la fuente del párrafo le faltan algunos de los caracteres nuevos: escrito con una fuente parecida (${font})`,
      reduced: (percent: string) => `reducido ${percent} para que quepa`,
      items: (n: string, one: boolean) => `${n} ${one ? 'elemento' : 'elementos'}`,
      annotations: (n: string, one: boolean) => `${n} ${one ? 'anotación' : 'anotaciones'}`,
      and: ' y ',
      moved: (points: string, what: string) => `lo que sigue ha bajado ${points} pt para hacerle sitio (${what})`,
      notMoved: 'Lo que sigue no se puede bajar:',
      wouldReachFooter: 'llegaría al pie de página',
      wouldPassBottom: 'pasaría del final de la página',
      needsPoints: (needed: string, free: string) =>
        `(hacen falta ${needed} pt y hay ${free} libres). Puedes acortar el texto para que quepa.`,
      insideBox: 'el párrafo está dentro de un recuadro (un marco, una celda) que no crece con él.',
      runInto: 'chocaría con lo que tiene al lado.',
      imageAcross: 'bajo el párrafo hay una imagen (una página escaneada, el fondo de un formulario).',
      drawingAcross: 'un dibujo atraviesa el final del párrafo (un recuadro, una tabla, una línea).',
      annotationAcross: 'una anotación atraviesa el final del párrafo.',
      nothingToMove: 'Debajo del párrafo no hay nada que mover.',
      cannotMove: 'aquí la página está dibujada de una forma que no se puede mover.',
      oneLine: 'Se modifica una línea cada vez.',
      oneParagraph: 'Se modifica un párrafo cada vez: estas líneas son de varios párrafos.',
      superscripts:
        'El párrafo tiene superíndices o subíndices, que el texto sin formato no puede expresar: modifícalo línea por línea.',
      onlyHorizontal: 'Solo se pueden recomponer los párrafos horizontales: modifícalo línea por línea.',
      lineBreak: 'El texto nuevo debe caber en una línea.',
      invisible: 'Aquí el texto es invisible (la capa de texto de un escaneo): no se puede modificar.',
      annotationText: 'Este texto pertenece a una anotación o a un campo del formulario: modifícalo allí.',
      standardFonts: 'Algunos caracteres no se pueden escribir ni con la fuente del texto ni con una parecida.',
      noText: 'Aquí no hay texto que modificar.',
      noRoom: 'El texto nuevo no cabe en el párrafo.',
    },
  },
);
