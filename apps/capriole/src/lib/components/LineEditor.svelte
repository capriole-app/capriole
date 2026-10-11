<script lang="ts">
  // A line of the page's own text written again in place: a box over the line, as tall as its text and
  // at least as wide, with the line's text in it and the cursor at its end. Enter (or leaving the box)
  // writes it — only what changed is given to the engine —, Esc leaves the line as it was.
  import { untrack } from 'svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { textEditorsWords } from '$lib/i18n/textEditors';
  import { getEditor, type LineEditing } from '$lib/editor.svelte';

  let { editing, width, height }: { editing: LineEditing; width: number; height: number } = $props();
  const editor = getEditor();
  const t = $derived(tr(textEditorsWords));

  // Points of the page in units of its width on screen (the page is a size container).
  const cqw = (points: number) => `${(points / width) * 100}cqw`;
  let finished = false;

  function start(input: HTMLInputElement) {
    input.value = untrack(() => editing.line.text);
    input.focus();
    input.setSelectionRange(input.value.length, input.value.length);
  }

  function finish(text: string | null) {
    if (finished) return;
    finished = true;
    editor.finishEditingLine(text);
  }

  const box = $derived(editing.line.box);
  // A little room around the line: the box's border must not hide its first and last letters.
  const kMargin = 2;
</script>

<input
  class="absolute z-10 [field-sizing:content] rounded-sm bg-white px-0.5 text-black shadow-sm outline-1 outline-sky-600 outline-solid"
  style:left="{((box[0] - kMargin) / width) * 100}%"
  style:top="{((box[1] - kMargin) / height) * 100}%"
  style:min-width="{((box[2] - box[0] + 2 * kMargin) / width) * 100}%"
  style:height="{((box[3] - box[1] + 2 * kMargin) / height) * 100}%"
  style:font-family="Helvetica, Arial, 'Liberation Sans', sans-serif"
  style:font-size={cqw(editing.line.size)}
  aria-label={t.line}
  data-testid="line-editor"
  {@attach start}
  onblur={(e) => finish(e.currentTarget.value)}
  onkeydown={(e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      finish(e.currentTarget.value);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      finish(null);
    }
  }}
/>
