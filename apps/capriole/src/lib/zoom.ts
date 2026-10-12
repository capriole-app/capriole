// Zoom: the steps − and + go through, and a percentage written by hand (clamped to the range).

/** The steps, in percent: − and + go to the next one down or up. */
const steps = [25, 33, 50, 67, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400];

export interface ZoomRange {
  min: number;
  max: number;
}

/** The pages grid: the thumbnails' size. */
export const gridRange: ZoomRange = { min: 50, max: 300 };
/** One page shown large: 100% is the page's size on paper (96 CSS pixels to the inch). */
export const pageRange: ZoomRange = { min: 25, max: 400 };

export const clampZoom = (percent: number, range: ZoomRange) => Math.min(range.max, Math.max(range.min, percent));

/** The next step up from percent (or the range's end). */
export function zoomIn(percent: number, range: ZoomRange): number {
  return clampZoom(steps.find((s) => s > percent + 0.5) ?? range.max, range);
}

/** The next step down from percent (or the range's start). */
export function zoomOut(percent: number, range: ZoomRange): number {
  return clampZoom([...steps].reverse().find((s) => s < percent - 0.5) ?? range.min, range);
}

/** A percentage written by hand ("150", "150%", "150,5 %"), clamped; null when it is not a number. */
export function parseZoom(text: string, range: ZoomRange): number | null {
  const n = Number.parseFloat(text.replace(',', '.').replace('%', '').trim());
  return Number.isFinite(n) && n > 0 ? Math.round(clampZoom(n, range)) : null;
}

/** CSS pixels per PDF point at 100%. */
export const pixelsPerPoint = 96 / 72;
