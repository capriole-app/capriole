<script lang="ts">
  // The page's words over its preview: transparent and selectable in reading order (the DOM order is the
  // engine's). Where a selection starts — a click puts the caret there — picks the word to describe.
  // Positions are fractions of the page, the font size a fraction of the layer's width (container units),
  // so the layer follows any zoom.
  import type { PageText } from '$lib/engine/types';

  let {
    text,
    selected,
    outlined,
    onselect,
  }: { text: PageText; selected: number | null; outlined: boolean; onselect: (word: number) => void } = $props();

  let layer = $state<HTMLDivElement>();

  const percent = (value: number, of: number) => `${(value / of) * 100}%`;

  function wordText(word: number): string {
    const [, , , , first, count] = text.words[word]!;
    let out = '';
    for (let c = first; c < first + count; c++) out += text.chars[c]![0];
    return out;
  }

  function selectionChanged() {
    const anchor = document.getSelection()?.anchorNode;
    const element = anchor instanceof Element ? anchor : anchor?.parentElement;
    const word = element?.closest<HTMLElement>('[data-word]');
    if (word && layer?.contains(word)) onselect(Number(word.dataset.word));
  }
</script>

<svelte:document onselectionchange={selectionChanged} />

<div bind:this={layer} class="@container absolute inset-0" data-testid="text-layer">
  {#each text.words as [x0, y0, x1, y1], i (i)}
    <span
      data-word={i}
      class={[
        'absolute cursor-text overflow-hidden leading-none whitespace-pre text-transparent selection:bg-primary/30',
        outlined && 'outline outline-1 outline-primary/50',
        selected === i && 'bg-primary/25 outline-2 outline-primary',
      ]}
      style:left={percent(x0, text.width)}
      style:top={percent(y0, text.height)}
      style:width={percent(x1 - x0, text.width)}
      style:height={percent(y1 - y0, text.height)}
      style:font-size="{((y1 - y0) * 0.75 * 100) / text.width}cqw">{wordText(i)}</span
    >
    <!-- the space between the spans separates the words of a selection -->
  {/each}
</div>
