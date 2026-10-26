<script lang="ts">
  // «Apri» in the bars (Toolbar, AnnotationToolbar), and Ctrl/Cmd+O: another PDF in place of the open one.
  // The file is chosen first, so that giving up keeps the document as it is; unsaved changes are asked
  // about before the choice, with the words of the bar's ✕. On a phone the button is in the bar's ⋯, which
  // calls choose() (the file's input stays here, in the bar: the menu closes before the file is chosen).
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import { getEditor } from '$lib/editor.svelte';
  import { choosesWhere, fromBrowser } from '$lib/platform';
  import { tr } from '$lib/i18n/index.svelte';
  import { toolbarWords } from '$lib/i18n/toolbar';
  import { barButton } from '$lib/ui';

  let {
    text,
    class: extra = '',
  }: {
    /** The class of the button's text: shown only where the bar has room for it. */
    text: string;
    /** The button's own: where it is hidden. */
    class?: string;
  } = $props();
  const editor = getEditor();
  const t = $derived(tr(toolbarWords));
  let input: HTMLInputElement | undefined = $state();

  export function choose(): void {
    if (editor.busy || (editor.modified && !confirm(t.closeUnsaved))) return;
    if (choosesWhere) void editor.replaceFromDialog();
    else input?.click();
  }

  function shortcut(e: KeyboardEvent): void {
    if (!(e.ctrlKey || e.metaKey) || e.key.toLowerCase() !== 'o' || e.shiftKey || e.altKey) return;
    e.preventDefault(); // not the browser's own «Open file»
    if (editor.typing || document.querySelector('dialog[open]')) return;
    choose();
  }
</script>

<svelte:window onkeydown={shortcut} />

<button
  type="button"
  class={[barButton, extra]}
  disabled={editor.busy}
  title={t.openTitle}
  data-testid="open-another"
  onclick={choose}
>
  <FolderOpen class="size-4" /> <span class={text}>{t.open}</span>
</button>
<input
  bind:this={input}
  type="file"
  accept="application/pdf,.pdf"
  class="hidden"
  data-testid="open-another-file"
  onchange={(e) => {
    const file = e.currentTarget.files?.[0];
    if (file) fromBrowser(file).then((picked) => editor.replace(picked));
    e.currentTarget.value = '';
  }}
/>
