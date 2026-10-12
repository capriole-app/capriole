<script lang="ts">
  // A free text written on the page itself: a box where the text will be, with the cursor in it, in the
  // engine's layout for free text (content/appearance: Helvetica, 2 points inside the box, lines 1.2 ×
  // the size apart). Made by a click it widens as the text is typed (Enter breaks the line); made by a
  // rectangle — or an existing text — its width stays, lines wrap at its edge and it grows downwards.
  // Leaving it (a click elsewhere, Esc) makes the text the annotation, its box measured as shown.
  import { untrack } from 'svelte';
  import { css } from '$lib/colors';
  import { tr } from '$lib/i18n/index.svelte';
  import { textEditorsWords } from '$lib/i18n/textEditors';
  import { getEditor, type Typing } from '$lib/editor.svelte';

  let { typing, width, height }: { typing: Typing; width: number; height: number } = $props();
  const editor = getEditor();
  const t = $derived(tr(textEditorsWords));

  // Points of the page in units of its width on screen (the page is a size container).
  const cqw = (points: number) => `${(points / width) * 100}cqw`;
  const kPadding = 2;
  let finished = false;

  /** The text in at first, the cursor at its end. */
  function start(box: HTMLElement) {
    box.textContent = untrack(() => typing.text);
    box.focus();
    const range = document.createRange();
    range.selectNodeContents(box);
    range.collapse(false);
    const selection = window.getSelection();
    selection?.removeAllRanges();
    selection?.addRange(range);
  }

  function finish(box: HTMLElement) {
    if (finished) return;
    finished = true;
    // The box as shown, in the page's points; a grown box keeps a point to spare (the engine wraps
    // the text when it does not fit, and the browser's font measures it a hair differently).
    const page = box.parentElement!.getBoundingClientRect();
    const r = box.getBoundingClientRect();
    const scale = width / page.width;
    const left = (r.left - page.left) * scale;
    const top = (r.top - page.top) * scale;
    const spare = typing.mode === 'grow' ? 2 : 0;
    const rect: Typing['rect'] = [left, top, left + r.width * scale + spare, top + r.height * scale];
    editor.finishTyping(box.innerText.replace(/\n$/, ''), rect);
  }

  const x0 = $derived(typing.rect[0]);
  const y0 = $derived(typing.rect[1]);
  const x1 = $derived(typing.rect[2]);
  const y1 = $derived(typing.rect[3]);
</script>

<div
  class={[
    'absolute z-10 cursor-text outline-1 outline-sky-600 outline-dashed',
    typing.mode === 'grow' ? 'whitespace-pre' : 'break-words whitespace-pre-wrap',
  ]}
  style:left="{(x0 / width) * 100}%"
  style:top="{(y0 / height) * 100}%"
  style:width={typing.mode === 'wrap' ? `${((x1 - x0) / width) * 100}%` : 'max-content'}
  style:min-width={cqw(typing.fontSize)}
  style:min-height="{((y1 - y0) / height) * 100}%"
  style:padding={cqw(kPadding)}
  style:font-family="Helvetica, Arial, 'Liberation Sans', sans-serif"
  style:font-size={cqw(typing.fontSize)}
  style:line-height="1.2"
  style:color={css(typing.color)}
  contenteditable="plaintext-only"
  role="textbox"
  tabindex="0"
  aria-multiline="true"
  aria-label={t.textBox}
  data-testid="text-box"
  {@attach start}
  onblur={(e) => finish(e.currentTarget)}
  onkeydown={(e) => {
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      e.currentTarget.blur();
    }
  }}
></div>
