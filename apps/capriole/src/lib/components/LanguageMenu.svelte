<script lang="ts">
  // The language of the interface: the system's at first, the person's choice once made (remembered).
  import Languages from '@lucide/svelte/icons/languages';
  import LanguageItems from './LanguageItems.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { menusWords } from '$lib/i18n/menus';
  import { barButton, menu } from '$lib/ui';

  const t = $derived(tr(menusWords));
  let open = $state(false);
</script>

<svelte:window onclick={() => (open = false)} onkeydown={(e) => e.key === 'Escape' && (open = false)} />

<div class="relative">
  <button
    type="button"
    class={barButton}
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
    <div role="menu" class={menu}><LanguageItems /></div>
  {/if}
</div>
