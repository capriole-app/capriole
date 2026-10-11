<script lang="ts">
  // A paragraph of the page's own text written again in place: a box over the paragraph, at least as large
  // as it, with its text in it (words broken at a line's end joined back) and the cursor at its end. Enter
  // starts a new line; Ctrl+Enter (or leaving the box) writes it — the engine lays it out anew inside the
  // paragraph —, Esc leaves it as it was.
  import { untrack } from 'svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { textEditorsWords } from '$lib/i18n/textEditors';
  import { getEditor, type ParagraphEditing } from '$lib/editor.svelte';

  let { editing, width, height }: { editing: ParagraphEditing; width: number; height: number } = $props();
  const editor = getEditor();
  const t = $derived(tr(textEditorsWords));

  // Points of the page in units of its width on screen (the page is a size container).
  const cqw = (points: number) => `${(points / width) * 100}cqw`;
  let finished = false;

  function start(area: HTMLTextAreaElement) {
    area.value = untrack(() => editing.text);
    area.focus();
    area.setSelectionRange(area.value.length, area.value.length);
  }

  function finish(text: string | null) {
    if (finished) return;
    finished = true;
    editor.finishEditingParagraph(text);
  }

  const box = $derived(editing.paragraph.box);
  // A little room around the paragraph: the box's border must not hide its edges.
  const kMargin = 2;
</script>

<textarea
  class="absolute z-10 resize-none rounded-sm bg-white px-1 text-black shadow-sm outline-1 outline-sky-600 outline-solid"
  style:left="{((box[0] - kMargin) / width) * 100}%"
  style:top="{((box[1] - kMargin) / height) * 100}%"
  style:width="{((box[2] - box[0] + 2 * kMargin) / width) * 100}%"
  style:min-height="{((box[3] - box[1] + 2 * kMargin) / height) * 100}%"
  style:field-sizing="content"
  style:font-family="Helvetica, Arial, 'Liberation Sans', sans-serif"
  style:font-size={cqw(editing.paragraph.size)}
  style:line-height="1.2"
  aria-label={t.paragraph}
  data-testid="paragraph-editor"
  {@attach start}
  onblur={(e) => finish(e.currentTarget.value)}
  onkeydown={(e) => {
    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      finish(e.currentTarget.value);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      finish(null);
    }
  }}></textarea>
