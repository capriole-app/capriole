<script lang="ts">
  // The language of the interface: the system's at first, the person's choice once made (remembered).
  // Each language is named in itself, so that someone who cannot read the current one finds theirs.
  import Check from '@lucide/svelte/icons/check';
  import Languages from '@lucide/svelte/icons/languages';
  import { i18n, languageNames, locales, tr } from '$lib/i18n/index.svelte';
  import { menusWords } from '$lib/i18n/menus';

  const t = $derived(tr(menusWords));
  let open = $state(false);
</script>

<svelte:window onclick={() => (open = false)} onkeydown={(e) => e.key === 'Escape' && (open = false)} />

<div class="relative">
  <button
    type="button"
    class="flex size-9 items-center justify-center rounded-md border hover:bg-muted"
    aria-label={t.language}
    title={t.language}
    aria-haspopup="menu"
    aria-expanded={open}
    onclick={(e) => {
      e.stopPropagation();
      open = !open;
    }}
  >
    <Languages class="size-4" />
  </button>
  {#if open}
    <div role="menu" class="absolute end-0 z-20 mt-1 w-36 rounded-md border bg-card p-1 shadow-md">
      {#each locales as locale (locale)}
        <button
          type="button"
          role="menuitemradio"
          aria-checked={i18n.locale === locale}
          lang={locale}
          class="flex w-full items-center gap-2 rounded px-2 py-1.5 text-sm hover:bg-muted"
          onclick={() => (i18n.locale = locale)}
        >
          <span class="flex-1 text-start">{languageNames[locale]}</span>
          {#if i18n.locale === locale}<Check class="size-4" />{/if}
        </button>
      {/each}
    </div>
  {/if}
</div>
