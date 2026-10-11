<script lang="ts">
  // Zoom: − and + go through the steps, the percentage can be written by hand (Enter or leaving the
  // field applies it, Escape puts back the one shown), and — for one page — «Fit» shows it whole.
  import Maximize from '@lucide/svelte/icons/maximize';
  import Minus from '@lucide/svelte/icons/minus';
  import Plus from '@lucide/svelte/icons/plus';
  import { parseZoom, zoomIn, zoomOut, type ZoomRange } from '$lib/zoom';
  import { tr } from '$lib/i18n/index.svelte';
  import { zoomWords } from '$lib/i18n/zoom';

  let {
    percent,
    range,
    onzoom,
    fitted = false,
    onfit,
  }: {
    /** The zoom shown, in percent (with fit: the one the page has now). */
    percent: number;
    range: ZoomRange;
    onzoom: (percent: number) => void;
    /** The page is fitted to the window (the fit button is pressed). */
    fitted?: boolean;
    /** When given: a button that fits the page to the window. */
    onfit?: () => void;
  } = $props();

  const t = $derived(tr(zoomWords));
  const shown = $derived(`${Math.round(percent)}%`);

  function apply(input: HTMLInputElement) {
    const value = parseZoom(input.value, range);
    if (value !== null && value !== Math.round(percent)) onzoom(value);
    else input.value = shown;
  }

  const button =
    'flex size-8 items-center justify-center rounded-md border hover:bg-muted disabled:pointer-events-none disabled:opacity-50';
</script>

<div class="flex items-center gap-1" role="group" aria-label={t.zoom} data-testid="zoom">
  <button
    type="button"
    class={button}
    aria-label={t.zoomOut}
    title={t.zoomOut}
    disabled={Math.round(percent) <= range.min}
    onclick={() => onzoom(zoomOut(percent, range))}><Minus class="size-4" /></button
  >
  <input
    class="w-16 rounded-md border bg-background px-1.5 py-1 text-center text-sm tabular-nums"
    aria-label={t.percent}
    value={shown}
    inputmode="decimal"
    spellcheck="false"
    onfocus={(e) => e.currentTarget.select()}
    onchange={(e) => apply(e.currentTarget)}
    onkeydown={(e) => {
      if (e.key === 'Enter') {
        // Applied, the keys go back to the page (Ctrl + 0 and the other shortcuts).
        e.preventDefault();
        apply(e.currentTarget);
        e.currentTarget.blur();
      } else if (e.key === 'Escape') {
        e.stopPropagation();
        e.currentTarget.value = shown;
        e.currentTarget.blur();
      }
    }}
  />
  <button
    type="button"
    class={button}
    aria-label={t.zoomIn}
    title={t.zoomIn}
    disabled={Math.round(percent) >= range.max}
    onclick={() => onzoom(zoomIn(percent, range))}><Plus class="size-4" /></button
  >
  {#if onfit}
    <button
      type="button"
      class={[button, 'w-auto gap-1.5 px-2 text-sm', fitted && 'bg-muted font-medium']}
      aria-pressed={fitted}
      title={t.fitTitle}
      onclick={onfit}><Maximize class="size-4" /> {t.fit}</button
    >
  {/if}
</div>
