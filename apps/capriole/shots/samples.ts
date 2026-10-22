// The documents the landing's screenshots show (shots.spec.ts): a contract between invented parties, in
// each language of the interface, written here as a PDF — never a real file, never one of the corpora.
// A4 pages, Helvetica and Helvetica-Bold (WinAnsiEncoding), paragraphs of lines 15 points apart: what the
// engine's paragraph editing reads as paragraphs.

export type Lang = 'it' | 'en' | 'fr' | 'de' | 'es';

interface Article {
  heading: string;
  paragraphs: string[];
}

interface Contract {
  kind: string;
  title: string;
  parties: string;
  articles: Article[];
  annexTitle: string;
  annexLines: string[];
  signatures: [string, string];
}

const contracts: Record<Lang, Contract> = {
  it: {
    kind: 'CONTRATTO DI COLLABORAZIONE',
    title: 'Studio Aurora e Giulia Marino',
    parties:
      'Tra Studio Aurora S.r.l., con sede in Via dei Tigli 12, Verona, e Giulia Marino, designer, si conviene quanto segue.',
    articles: [
      {
        heading: 'Art. 1 - Oggetto',
        paragraphs: [
          'La collaboratrice cura il progetto grafico della nuova collana editoriale dello Studio, dalle prime bozze fino ai file pronti per la stampa, secondo le indicazioni concordate.',
        ],
      },
      {
        heading: 'Art. 2 - Durata',
        paragraphs: [
          'Il presente incarico ha una durata di sei mesi a decorrere dalla data di sottoscrizione, e potrà essere rinnovato per iscritto con il consenso di entrambe le parti.',
        ],
      },
      {
        heading: 'Art. 3 - Compenso',
        paragraphs: [
          "Per l'attività svolta è riconosciuto un compenso mensile di 1.800,00 euro, da corrispondere entro il giorno 10 del mese successivo, dietro presentazione di fattura.",
          'Le spese di viaggio preventivamente autorizzate sono rimborsate a parte, su presentazione dei giustificativi.',
        ],
      },
      {
        heading: 'Art. 4 - Riservatezza',
        paragraphs: [
          "Le parti si impegnano a mantenere riservate tutte le informazioni ricevute nel corso dell'incarico, anche dopo la sua conclusione, salvo obbligo di legge.",
        ],
      },
      {
        heading: 'Art. 5 - Recesso',
        paragraphs: [
          'Ciascuna parte può recedere con un preavviso scritto di trenta giorni. I lavori consegnati fino a quel momento restano allo Studio e sono pagati per intero.',
        ],
      },
    ],
    annexTitle: 'Allegato A - Calendario delle consegne',
    annexLines: [
      'Prime bozze della copertina: entro il 15 novembre',
      'Impaginato del primo volume: entro il 10 dicembre',
      'File per la stampa: entro il 20 gennaio',
    ],
    signatures: ['Per Studio Aurora S.r.l.', 'Giulia Marino'],
  },
  en: {
    kind: 'SERVICES AGREEMENT',
    title: 'Aurora Studio and Julia Marsh',
    parties:
      'Between Aurora Studio Ltd, 12 Linden Road, Bristol, and Julia Marsh, designer, the parties agree as follows.',
    articles: [
      {
        heading: '1. Scope',
        paragraphs: [
          "The designer is responsible for the graphic design of the Studio's new book series, from the first sketches to the files ready for print, as agreed between the parties.",
        ],
      },
      {
        heading: '2. Term',
        paragraphs: [
          'This agreement lasts six months from the date of signature, and may be renewed in writing with the consent of both parties.',
        ],
      },
      {
        heading: '3. Fees',
        paragraphs: [
          'For the work done the designer receives a monthly fee of 1,800.00 pounds, payable by the 10th of the following month against an invoice.',
          'Travel expenses approved in advance are refunded separately, against receipts.',
        ],
      },
      {
        heading: '4. Confidentiality',
        paragraphs: [
          'The parties keep confidential all the information received during the work, also after it ends, unless the law requires otherwise.',
        ],
      },
      {
        heading: '5. Termination',
        paragraphs: [
          'Either party may end this agreement with thirty days written notice. The work delivered until then belongs to the Studio and is paid in full.',
        ],
      },
    ],
    annexTitle: 'Annex A - Delivery schedule',
    annexLines: [
      'First cover sketches: by 15 November',
      'Layout of the first volume: by 10 December',
      'Files for print: by 20 January',
    ],
    signatures: ['For Aurora Studio Ltd', 'Julia Marsh'],
  },
  fr: {
    kind: 'CONTRAT DE PRESTATION',
    title: 'Atelier Aurore et Julie Marin',
    parties:
      'Entre Atelier Aurore SARL, 12 rue des Tilleuls, Lyon, et Julie Marin, graphiste, il est convenu ce qui suit.',
    articles: [
      {
        heading: 'Article 1 - Objet',
        paragraphs: [
          "La graphiste assure la conception graphique de la nouvelle collection de l'Atelier, des premières esquisses jusqu'aux fichiers prêts pour l'impression, selon les indications convenues.",
        ],
      },
      {
        heading: 'Article 2 - Durée',
        paragraphs: [
          "Le présent contrat est conclu pour une durée de six mois à compter de sa signature, et peut être renouvelé par écrit avec l'accord des deux parties.",
        ],
      },
      {
        heading: 'Article 3 - Rémunération',
        paragraphs: [
          "Pour le travail accompli, la graphiste perçoit une rémunération mensuelle de 1 800,00 euros, payable avant le 10 du mois suivant sur présentation d'une facture.",
          "Les frais de déplacement autorisés à l'avance sont remboursés à part, sur justificatifs.",
        ],
      },
      {
        heading: 'Article 4 - Confidentialité',
        paragraphs: [
          "Les parties s'engagent à garder confidentielles toutes les informations reçues pendant la mission, y compris après sa fin, sauf obligation légale.",
        ],
      },
      {
        heading: 'Article 5 - Résiliation',
        paragraphs: [
          "Chaque partie peut résilier le contrat avec un préavis écrit de trente jours. Les travaux livrés jusque-là restent à l'Atelier et sont payés en totalité.",
        ],
      },
    ],
    annexTitle: 'Annexe A - Calendrier des livraisons',
    annexLines: [
      'Premières esquisses de la couverture : avant le 15 novembre',
      'Mise en page du premier volume : avant le 10 décembre',
      "Fichiers pour l'impression : avant le 20 janvier",
    ],
    signatures: ['Pour Atelier Aurore SARL', 'Julie Marin'],
  },
  de: {
    kind: 'DIENSTLEISTUNGSVERTRAG',
    title: 'Atelier Aurora und Julia Marx',
    parties:
      'Zwischen Atelier Aurora GmbH, Lindenstraße 12, Leipzig, und Julia Marx, Designerin, wird Folgendes vereinbart.',
    articles: [
      {
        heading: '§ 1 Gegenstand',
        paragraphs: [
          'Die Designerin übernimmt die Gestaltung der neuen Buchreihe des Ateliers, von den ersten Entwürfen bis zu den druckfertigen Dateien, nach den vereinbarten Vorgaben.',
        ],
      },
      {
        heading: '§ 2 Laufzeit',
        paragraphs: [
          'Der Vertrag läuft sechs Monate ab dem Tag der Unterzeichnung und kann im Einvernehmen beider Parteien schriftlich verlängert werden.',
        ],
      },
      {
        heading: '§ 3 Vergütung',
        paragraphs: [
          'Für die geleistete Arbeit erhält die Designerin eine monatliche Vergütung von 1.800,00 Euro, zahlbar bis zum 10. des Folgemonats gegen Rechnung.',
          'Vorab genehmigte Reisekosten werden gegen Beleg gesondert erstattet.',
        ],
      },
      {
        heading: '§ 4 Vertraulichkeit',
        paragraphs: [
          'Die Parteien behandeln alle während des Auftrags erhaltenen Informationen vertraulich, auch nach seinem Ende, soweit das Gesetz nichts anderes verlangt.',
        ],
      },
      {
        heading: '§ 5 Kündigung',
        paragraphs: [
          'Jede Partei kann mit einer Frist von dreißig Tagen schriftlich kündigen. Die bis dahin gelieferten Arbeiten gehören dem Atelier und werden voll bezahlt.',
        ],
      },
    ],
    annexTitle: 'Anlage A - Lieferplan',
    annexLines: [
      'Erste Entwürfe des Umschlags: bis 15. November',
      'Satz des ersten Bandes: bis 10. Dezember',
      'Druckdateien: bis 20. Januar',
    ],
    signatures: ['Für Atelier Aurora GmbH', 'Julia Marx'],
  },
  es: {
    kind: 'CONTRATO DE COLABORACIÓN',
    title: 'Estudio Aurora y Julia Marín',
    parties:
      'Entre Estudio Aurora S.L., con domicilio en Calle de los Tilos 12, Valencia, y Julia Marín, diseñadora, se acuerda lo siguiente.',
    articles: [
      {
        heading: 'Cláusula 1 - Objeto',
        paragraphs: [
          'La colaboradora se encarga del diseño gráfico de la nueva colección del Estudio, desde los primeros bocetos hasta los archivos listos para imprenta, según lo acordado.',
        ],
      },
      {
        heading: 'Cláusula 2 - Duración',
        paragraphs: [
          'El presente encargo tiene una duración de seis meses desde la fecha de firma, y podrá renovarse por escrito con el acuerdo de ambas partes.',
        ],
      },
      {
        heading: 'Cláusula 3 - Retribución',
        paragraphs: [
          'Por el trabajo realizado se reconoce una retribución mensual de 1.800,00 euros, pagadera antes del día 10 del mes siguiente contra factura.',
          'Los gastos de viaje autorizados previamente se reembolsan aparte, con los justificantes.',
        ],
      },
      {
        heading: 'Cláusula 4 - Confidencialidad',
        paragraphs: [
          'Las partes se comprometen a mantener reservada toda la información recibida durante el encargo, también después de su fin, salvo obligación legal.',
        ],
      },
      {
        heading: 'Cláusula 5 - Rescisión',
        paragraphs: [
          'Cada parte puede rescindir el contrato con un preaviso escrito de treinta días. Los trabajos entregados hasta entonces quedan para el Estudio y se pagan íntegramente.',
        ],
      },
    ],
    annexTitle: 'Anexo A - Calendario de entregas',
    annexLines: [
      'Primeros bocetos de la cubierta: antes del 15 de noviembre',
      'Maquetación del primer volumen: antes del 10 de diciembre',
      'Archivos para imprenta: antes del 20 de enero',
    ],
    signatures: ['Por Estudio Aurora S.L.', 'Julia Marín'],
  },
};

/** The sentence the text scene rewrites, and what it becomes (article 2, in each language). */
export const edit: Record<Lang, { from: string; to: string }> = {
  it: { from: 'sei mesi', to: 'dodici mesi' },
  en: { from: 'six months', to: 'twelve months' },
  fr: { from: 'six mois', to: 'douze mois' },
  de: { from: 'sechs Monate', to: 'zwölf Monate' },
  es: { from: 'seis meses', to: 'doce meses' },
};

/** The contract's article 2, as its paragraph reads (the text scene finds it by this). */
export function termParagraph(lang: Lang): string {
  return contracts[lang].articles[1]!.paragraphs[0]!;
}

export const width = 595;
export const height = 842;
const margin = 72;
const size = 11;
const leading = 15;

/** Helvetica's advance widths (thousandths of an em) for the printable ASCII range; others: 556. */
const helvetica = [
  278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556,
  556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833,
  722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556,
  556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334,
  260, 334, 584,
];

function advance(text: string, points: number): number {
  let total = 0;
  for (const c of text) {
    const code = c.codePointAt(0)!;
    total += code >= 32 && code < 127 ? helvetica[code - 32]! : 556;
  }
  return (total * points) / 1000;
}

/** Lines of at most `room` points. */
function wrap(text: string, room: number): string[] {
  const lines: string[] = [];
  let line = '';
  for (const word of text.split(' ')) {
    const next = line ? `${line} ${word}` : word;
    if (line && advance(next, size) > room) {
      lines.push(line);
      line = word;
    } else line = next;
  }
  if (line) lines.push(line);
  return lines;
}

/** A string in a content stream: WinAnsi bytes (the texts use Latin-1 letters only), escaped. */
function literal(text: string): string {
  return '(' + text.replace(/[\\()]/g, (c) => '\\' + c) + ')';
}

class Page {
  ops: string[] = [];
  y = height - margin;

  text(font: 'F1' | 'F2', points: number, x: number, line: string) {
    this.ops.push(`BT /${font} ${points} Tf ${x} ${this.y.toFixed(2)} Td ${literal(line)} Tj ET`);
  }

  paragraph(text: string) {
    for (const line of wrap(text, width - 2 * margin)) {
      this.text('F1', size, margin, line);
      this.y -= leading;
    }
    this.y -= 8;
  }

  heading(text: string) {
    this.y -= 6;
    this.text('F2', 12, margin, text);
    this.y -= 30; // apart from its paragraph: read as one more line of it otherwise
  }

  rule(x1: number, x2: number) {
    this.ops.push(`0.4 w ${x1} ${this.y} m ${x2} ${this.y} l S`);
  }
}

/** A spot on a page, in points from its bottom-left corner. */
export interface Spot {
  page: number;
  x: number;
  y: number;
}

/** The contract in `lang`: seven pages (the articles, the signatures, the annexes), as a PDF file; and where
 *  its article 2 (the term: the text scene) and article 3's first paragraph (the fees: highlighted) are. */
export function contract(lang: Lang): { pdf: Buffer; term: Spot; fees: Spot } {
  const c = contracts[lang];
  const pages: Page[] = [];
  let page = new Page();
  pages.push(page);
  page.text('F1', 9, margin, c.kind);
  page.y -= 26;
  page.text('F2', 20, margin, c.title);
  page.y -= 32;
  page.paragraph(c.parties);
  const spots: Spot[] = [];
  c.articles.forEach((article, i) => {
    if (i === 3) {
      page = new Page();
      pages.push(page);
    }
    page.heading(article.heading);
    spots.push({ page: pages.length - 1, x: margin, y: page.y });
    for (const p of article.paragraphs) page.paragraph(p);
  });
  // The signatures, at the foot of the second page.
  page.y = 190;
  for (const [i, name] of c.signatures.entries()) {
    const x = margin + i * 250;
    page.ops.push(`0.4 w ${x} ${page.y} m ${x + 200} ${page.y} l S`);
    page.ops.push(`BT /F1 10 Tf ${x} ${page.y - 16} Td ${literal(name)} Tj ET`);
  }
  // The annex, and a page of its notes.
  page = new Page();
  pages.push(page);
  page.heading(c.annexTitle);
  for (const line of c.annexLines) {
    page.text('F1', size, margin + 14, `-  ${line}`);
    page.y -= 22;
  }
  // More annexes, so that the grid of pages is full: each takes two of the articles again.
  for (const [i, letter] of ['B', 'C', 'D', 'E'].entries()) {
    page = new Page();
    pages.push(page);
    page.heading(c.annexTitle.replace(/A\b/, letter));
    for (const p of c.articles.slice(i % 4, (i % 4) + 2)) page.paragraph(p.paragraphs[0]!);
  }

  const fonts =
    '<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>' +
    '/F2<</Type/Font/Subtype/Type1/BaseFont/Helvetica-Bold/Encoding/WinAnsiEncoding>>>>';
  const first = 3;
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    `<</Type/Pages/Kids[${pages.map((_, i) => `${first + 2 * i} 0 R`).join(' ')}]/Count ${pages.length}>>`,
  ];
  for (const [i, p] of pages.entries()) {
    const content = p.ops.join('\n');
    bodies.push(
      `<</Type/Page/Parent 2 0 R/MediaBox[0 0 ${width} ${height}]/Contents ${first + 2 * i + 1} 0 R/Resources<</Font${fonts}>>>>`,
      `<</Length ${Buffer.byteLength(content, 'latin1')}>>\nstream\n${content}\nendstream`,
    );
  }
  let text = '%PDF-1.7\n';
  const offsets: number[] = [];
  bodies.forEach((body, i) => {
    offsets.push(Buffer.byteLength(text, 'latin1'));
    text += `${i + 1} 0 obj\n${body}\nendobj\n`;
  });
  const xref = Buffer.byteLength(text, 'latin1');
  text += `xref\n0 ${bodies.length + 1}\n0000000000 65535 f \n`;
  text += offsets.map((o) => `${String(o).padStart(10, '0')} 00000 n \n`).join('');
  text += `trailer\n<</Size ${bodies.length + 1}/Root 1 0 R>>\nstartxref\n${xref}\n%%EOF\n`;
  return { pdf: Buffer.from(text, 'latin1'), term: spots[1]!, fees: spots[2]! };
}

/** The file's name in each language. */
export const fileName: Record<Lang, string> = {
  it: 'contratto.pdf',
  en: 'agreement.pdf',
  fr: 'contrat.pdf',
  de: 'vertrag.pdf',
  es: 'contrato.pdf',
};
