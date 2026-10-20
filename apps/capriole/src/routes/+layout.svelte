<script lang="ts">
  import '../app.css';
  import { untrack } from 'svelte';
  import { ModeWatcher } from 'mode-watcher';
  import { page } from '$app/state';
  import { replaceState } from '$app/navigation';
  import { i18n, isLocale, tr } from '$lib/i18n/index.svelte';
  import { headWords } from '$lib/i18n/head';

  let { children } = $props();

  // The title and description, written per page at build time, in the interface's language (the tab,
  // bookmarks).
  $effect(() => {
    const words = tr(headWords);
    document.title = words.title;
    document.querySelector('meta[name="description"]')?.setAttribute('content', words.description);
  });

  // On a language's page (/it/…), the address follows the language chosen from the menu, so that a
  // bookmark or a reload opens it in that language; / stays / (the choice is remembered there).
  $effect(() => {
    const locale = i18n.locale;
    const first = location.pathname.split('/')[1];
    if (isLocale(first) && first !== locale) {
      // page.state only carried along: the effect depends on the language alone.
      replaceState(
        `/${locale}/${location.search}${location.hash}`,
        untrack(() => page.state),
      );
    }
  });
</script>

<ModeWatcher disableHeadScriptInjection />
{@render children()}
