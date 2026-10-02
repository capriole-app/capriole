// The colours offered for annotations (RGB, 0..1).
export const colors = [
  { name: 'Giallo', rgb: [1, 0.82, 0] },
  { name: 'Verde', rgb: [0.2, 0.75, 0.3] },
  { name: 'Blu', rgb: [0.15, 0.45, 1] },
  { name: 'Rosso', rgb: [0.9, 0.15, 0.15] },
  { name: 'Nero', rgb: [0, 0, 0] },
] as const satisfies { name: string; rgb: readonly [number, number, number] }[];

export function sameColor(a: readonly number[] | null, b: readonly number[]): boolean {
  return a !== null && a.length === 3 && a.every((v, i) => Math.abs(v - b[i]!) < 0.01);
}

export function css(rgb: readonly number[]): string {
  return `rgb(${rgb.map((v) => Math.round(v * 255)).join(',')})`;
}
