<script lang="ts">
  import '../app.css';
  import { ModeWatcher } from 'mode-watcher';
  import { tr } from '$lib/i18n/index.svelte';
  import { headWords } from '$lib/i18n/head';

  let { children } = $props();

  // The title and description app.html has in English, in the interface's language (the tab, bookmarks).
  $effect(() => {
    const words = tr(headWords);
    document.title = words.title;
    document.querySelector('meta[name="description"]')?.setAttribute('content', words.description);
  });
</script>

<ModeWatcher disableHeadScriptInjection />
{@render children()}
