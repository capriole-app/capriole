// The objects of a page shown that can be moved, resized and deleted (phase 10c): images, drawings and
// shadings of the page's model (the engine's, phase 10a) — not its text, nor its background — and how
// to say what they are and why an edit of one was refused.
import type { PageElement } from '@capriole/engine-client';
import { tr } from './i18n/index.svelte';
import { objectWords } from './i18n/objects';

/** Whether an element of the page's model is an object the person can move, resize and delete. */
export function editable(e: PageElement): boolean {
  return (e.kind === 'image' || e.kind === 'drawing' || e.kind === 'shading') && e.role !== 'background';
}

const roles = ['rule', 'box', 'table', 'figure'] as const;
const isRole = (role: string): role is (typeof roles)[number] => (roles as readonly string[]).includes(role);

/** What an object is, in a word. */
export function objectName(e: PageElement): string {
  const t = tr(objectWords);
  if (e.kind === 'image') return t.image;
  if (e.kind === 'shading') return t.shading;
  return isRole(e.role) ? t[e.role] : t.figure;
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

type Words = (typeof objectWords)['it'];

const refusals: [RegExp, keyof Words][] = [
  [/leave the page/, 'leavePage'],
  [/leave what clips it/, 'leaveClip'],
  [/also clips/, 'alsoClips'],
  [/soft mask/, 'softMask'],
  [/it is text/, 'isText'],
  [/annotation/, 'isAnnotation'],
  [/size must be/, 'badSize'],
  [/cannot edit the object/, 'cannotEdit'],
];

/** The engine's refusal of an object's edit, in the interface's language (what is not recognised, as it is). */
export function objectRefusal(message: string): string {
  const key = refusals.find(([pattern]) => pattern.test(message))?.[1];
  return key ? tr(objectWords)[key] : message;
}
