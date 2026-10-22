<script lang="ts">
  import '../app.css';
  import { onMount, untrack } from 'svelte';
  import { ModeWatcher } from 'mode-watcher';
  import { page } from '$app/state';
  import { replaceState } from '$app/navigation';
  import { i18n, isLocale, remembered, tr } from '$lib/i18n/index.svelte';
  import { headWords } from '$lib/i18n/head';

  let { children } = $props();

  // The title and description, written per page at build time, in the interface's language (the tab,
  // bookmarks).
  $effect(() => {
    const words = tr(headWords);
    document.title = words.title;
    document.querySelector('meta[name="description"]')?.setAttribute('content', words.description);
  });

  // The address follows the language (/ is English, /it/ Italian…), so that a bookmark or a reload opens the
  // page in it: chosen from the menu, or chosen before and remembered — then / takes the person to their
  // language's page, a moment after the English one is shown.
  onMount(() => {
    const chosen = remembered();
    if (chosen && !isLocale(location.pathname.split('/')[1])) i18n.visit(chosen);
  });
  $effect(() => {
    const locale = i18n.locale;
    const first = location.pathname.split('/')[1];
    if ((isLocale(first) ? first : 'en') !== locale) {
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
