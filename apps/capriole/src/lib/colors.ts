// The colours offered for annotations (RGB, 0..1); their names in the interface's language.
import { tr } from './i18n/index.svelte';
import { colorWords } from './i18n/colors';

function color(key: keyof (typeof colorWords)['it'], rgb: readonly [number, number, number]) {
  return {
    get name(): string {
      return tr(colorWords)[key];
    },
    rgb,
  };
}

export const colors = [
  color('yellow', [1, 0.82, 0]),
  color('green', [0.2, 0.75, 0.3]),
  color('blue', [0.15, 0.45, 1]),
  color('red', [0.9, 0.15, 0.15]),
  color('black', [0, 0, 0]),
] as const;

export function sameColor(a: readonly number[] | null, b: readonly number[]): boolean {
  return a !== null && a.length === 3 && a.every((v, i) => Math.abs(v - b[i]!) < 0.01);
}

export function css(rgb: readonly number[]): string {
  return `rgb(${rgb.map((v) => Math.round(v * 255)).join(',')})`;
}
