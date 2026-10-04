// What the engine says of a conversion to PDF/A (edit/pdfa.hpp), in Italian: what it put right, and why it
// refused. The engine's texts have a fixed form ("kind: count (details)", one reason each); what is not
// recognised is shown as it is.

const changes: [RegExp, string][] = [
  [/^output intent added/, 'Profilo colore aggiunto (sRGB)'],
  [/^output intent kept/, 'Profilo colore del documento mantenuto'],
  [/^fonts embedded \(substitutes/, 'Font incorporati (sostituti con le stesse metriche)'],
  [/^TrueType programs given the widths/, 'Larghezze dei font TrueType allineate a quelle del documento'],
  [/^widths written for embedded fonts/, 'Larghezze scritte per i font che non le dichiaravano'],
  [/^widths written for fonts of invisible text/, 'Larghezze scritte per i font del testo invisibile'],
  [/^\/CIDSet removed|^\/CharSet removed/, 'Dati dei font non più necessari tolti'],
  [/^TrueType CIDFonts given/, 'Mappe dei glifi dei font TrueType completate'],
  [/^fonts written in place/, 'Font riorganizzati come oggetti propri'],
  [/^metadata written/, 'Metadati scritti (identificazione PDF/A)'],
  [/^metadata of objects removed/, 'Metadati di immagini e altri oggetti tolti'],
  [/^Factur-X properties kept/, 'Dati della fattura Factur-X mantenuti nei metadati'],
  [/^annotations removed \(hidden/, 'Annotazioni nascoste o di tipi non ammessi tolte'],
  [/^annotations removed \(no appearance/, 'Annotazioni senza aspetto tolte'],
  [/^annotations made printable/, 'Annotazioni rese stampabili'],
  [/^annotation appearances made/, 'Aspetto delle annotazioni generato'],
  [/^appearances reduced/, 'Aspetti delle annotazioni semplificati'],
  [/^actions PDF\/A forbids removed/, 'Azioni non ammesse tolte (JavaScript, avvio di programmi…)'],
  [/^additional actions/, 'Azioni automatiche tolte'],
  [/^document JavaScript removed/, 'JavaScript del documento tolto'],
  [/^\/NeedAppearances|^\/XFA|^\/NeedsRendering|^\/Requirements/, 'Impostazioni dei moduli non ammesse tolte'],
  [/^form entries that are not fields/, 'Voci del modulo non valide tolte'],
  [/^image interpolation/, 'Interpolazione delle immagini disattivata'],
  [/^image \/(Alternates|OPI)|^form \/OPI/, 'Versioni alternative delle immagini tolte'],
  [/^transfer functions/, 'Funzioni di trasferimento tolte'],
  [/^LZW streams/, 'Dati compressi con LZW ricompressi'],
  [/^optional content/, 'Livelli (contenuto opzionale) sistemati'],
  [/^inherited resources/, 'Risorse ereditate assegnate alle pagine'],
  [/^page contents that are not streams/, 'Contenuti di pagina non validi tolti'],
  [/^embedded files made PDF\/A-3/, 'Allegati resi conformi a PDF/A-3'],
];

/** A change the conversion made, in Italian, with its count and details. */
export function pdfaChange(line: string): string {
  const m = /^(.*?): (\d+)(?: \((.*)\))?$/.exec(line);
  const kind = m?.[1] ?? line;
  const translated = changes.find(([pattern]) => pattern.test(kind))?.[1];
  if (!translated) return line;
  const count = m?.[2] && m[2] !== '1' ? ` (${m[2]})` : '';
  const details = m?.[3] ? `: ${m[3]}` : '';
  return `${translated}${count}${details}`;
}

const reasons: [RegExp, (m: RegExpExecArray) => string][] = [
  [
    /document is signed/,
    () => 'Il documento è firmato: la conversione invaliderebbe le firme. Converti prima di firmare.',
  ],
  [
    /encrypted/,
    () =>
      'Il documento è protetto da password: PDF/A non ammette la cifratura (la rimozione non è ancora disponibile).',
  ],
  [
    /^font (.+?) is not embedded and no font with the same metrics/,
    (m) => `Il font «${m[1]}» non è incorporato e non c'è un sostituto con le stesse metriche.`,
  ],
  [
    /^font (.+?) is not embedded and its metrics are not those/,
    (m) => `Il font «${m[1]}» non è incorporato e le sue metriche non coincidono con quelle del sostituto.`,
  ],
  [/^font (.+?) is not embedded/, (m) => `Il font «${m[1]}» non è incorporato e non ha un sostituto.`],
  [
    /^font (.+?): a character it shows has no glyph in its embedded/,
    (m) => `Il font «${m[1]}» mostra caratteri che non contiene.`,
  ],
  [/^font (.+?): a character it shows .*substitute/, (m) => `Il font «${m[1]}» usa caratteri che il sostituto non ha.`],
  [/^font (.+?): the widths/, (m) => `Il font «${m[1]}» ha larghezze incoerenti con il suo programma.`],
  [/DeviceCMYK/, () => 'Il documento usa colori CMYK: servirebbe un profilo di stampa CMYK (non ancora disponibile).'],
  [/DeviceRGB/, () => 'Il documento usa colori RGB con un profilo di stampa diverso.'],
  [/operators PDF does not define/, () => 'Il contenuto usa comandi non previsti dallo standard PDF.'],
  [/filter that is not a standard/, () => 'Il file contiene dati compressi in un formato non standard.'],
  [/not UTF-8/, () => 'Il file contiene nomi non validi (non UTF-8).'],
  [/embedded files/, () => 'Il documento ha allegati: PDF/A-2 non li ammette, serve PDF/A-3.'],
  [
    /font resource that is not a font|not an object of its own/,
    () => 'Il file contiene riferimenti a font non validi.',
  ],
];

/** Why the engine refused, in Italian. */
export function pdfaReason(reason: string): string {
  for (const [pattern, text] of reasons) {
    const m = pattern.exec(reason);
    if (m) return text(m);
  }
  return reason;
}
