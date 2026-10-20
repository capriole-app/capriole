<script lang="ts">
  // The languages as a menu's items (LanguageMenu, MoreMenu). Each is named in itself, so that someone who
  // cannot read the current one finds theirs.
  import Check from '@lucide/svelte/icons/check';
  import { i18n, languageNames, locales } from '$lib/i18n/index.svelte';
  import { menuItem } from '$lib/ui';
</script>

{#each locales as locale (locale)}
  <button
    type="button"
    role="menuitemradio"
    aria-checked={i18n.locale === locale}
    lang={locale}
    class={menuItem}
    onclick={() => (i18n.locale = locale)}
  >
    <span class="flex-1">{languageNames[locale]}</span>
    {#if i18n.locale === locale}<Check class="size-4 text-emphasis" />{/if}
  </button>
{/each}
