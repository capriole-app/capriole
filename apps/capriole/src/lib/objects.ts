// The objects of a page shown that can be moved, resized and deleted (phase 10c): images, drawings and
// shadings of the page's model (the engine's, phase 10a) — not its text, nor its background — and how
// to say what they are and why an edit of one was refused.
import type { PageElement } from '@capriole/engine-client';

/** Whether an element of the page's model is an object the person can move, resize and delete. */
export function editable(e: PageElement): boolean {
  return (e.kind === 'image' || e.kind === 'drawing' || e.kind === 'shading') && e.role !== 'background';
}

const roles: Record<string, string> = {
  rule: 'Filetto',
  box: 'Riquadro',
  table: 'Tabella',
  figure: 'Disegno',
};

/** What an object is, in a word. */
export function objectName(e: PageElement): string {
  if (e.kind === 'image') return 'Immagine';
  if (e.kind === 'shading') return 'Sfumatura';
  return roles[e.role] ?? 'Disegno';
}

/**
 * The object under a point of the page (as shown): of those whose box holds it, the smallest — the one on
 * top, mostly (a rule in a box, a picture in a frame). Its index in the model's elements.
 */
export function objectAt(elements: PageElement[], x: number, y: number, margin = 2): number | null {
  let best: number | null = null;
  let area = Infinity;
  elements.forEach((e, i) => {
    const [x0, y0, x1, y1] = e.box;
    if (!editable(e) || x < x0 - margin || x > x1 + margin || y < y0 - margin || y > y1 + margin) return;
    const a = (x1 - x0) * (y1 - y0);
    if (a < area) {
      best = i;
      area = a;
    }
  });
  return best;
}

/** After an edit, the object of that kind nearest where it went (the model is read again: indices change). */
export function nearest(elements: PageElement[], kind: PageElement['kind'], box: readonly number[]): number | null {
  let best: number | null = null;
  let distance = Infinity;
  elements.forEach((e, i) => {
    if (e.kind !== kind && !(kind !== 'image' && e.kind === 'drawing')) return;
    const d = e.box.reduce((sum, v, k) => sum + Math.abs(v - box[k]!), 0);
    if (d < distance) {
      best = i;
      distance = d;
    }
  });
  return best;
}

const refusals: [RegExp, string][] = [
  [/leave the page/, 'Così l’oggetto uscirebbe dalla pagina.'],
  [
    /leave what clips it/,
    'Così l’oggetto uscirebbe dal riquadro che lo contiene (una cornice, un gruppo): si vedrebbe tagliato.',
  ],
  [/also clips/, 'Questo tracciato ritaglia anche ciò che viene dopo: si può solo eliminare.'],
  [/soft mask/, 'L’oggetto è disegnato attraverso una maschera di trasparenza che non si sposta con lui.'],
  [/it is text/, 'È testo: si modifica con «Modifica testo».'],
  [/annotation/, 'È un’annotazione: si sposta come le annotazioni.'],
  [/size must be/, 'La dimensione nuova non è valida.'],
  [/cannot edit the object/, 'Questo oggetto non si può modificare qui.'],
];

/** The engine's refusal of an object's edit, in Italian (what is not recognised, as it is). */
export function objectRefusal(message: string): string {
  return refusals.find(([pattern]) => pattern.test(message))?.[1] ?? message;
}
