<script lang="ts">
  // One page, rendered twice. At build time (and in the browser's first paint) the landing alone: words in
  // the page's language, no engine — what search engines read. Then, in the browser, the app's code is
  // loaded and the editor made (lib/editor-context.ts): the landing's card opens files from then on, and a
  // file open shows the app in its place.
  import { onMount, type Component } from 'svelte';
  import Landing from '$lib/components/landing/Landing.svelte';
  import { setApp } from '$lib/editor-context';
  import type { Editor } from '$lib/editor.svelte';

  let editor = $state.raw<Editor>();
  let App = $state.raw<Component<{ editor: Editor }>>();
  let failed = $state(false);

  setApp({
    get editor() {
      return editor;
    },
    get failed() {
      return failed;
    },
  });

  onMount(() => {
    Promise.all([import('$lib/editor.svelte'), import('$lib/components/EditorApp.svelte')]).then(
      ([module, app]) => {
        editor = new module.Editor();
        App = app.default;
      },
      (error: unknown) => {
        console.error(error);
        failed = true;
      },
    );
  });
</script>

{#if editor && App}<App {editor} />{/if}
{#if !editor?.name}<Landing />{/if}
