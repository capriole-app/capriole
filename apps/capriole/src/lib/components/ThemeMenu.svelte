<script lang="ts">
  // The theme: light, dark or the system's.
  import Sun from '@lucide/svelte/icons/sun';
  import Moon from '@lucide/svelte/icons/moon';
  import ThemeItems from './ThemeItems.svelte';
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
    aria-label={t.theme}
    title={t.theme}
    aria-haspopup="menu"
    aria-expanded={open}
    onclick={(e) => {
      e.stopPropagation();
      open = !open;
    }}
  >
    <Sun class="size-4 dark:hidden" />
    <Moon class="hidden size-4 dark:block" />
  </button>
  {#if open}
    <div role="menu" class={menu}><ThemeItems /></div>
  {/if}
</div>
