<script lang="ts">
  // Light / dark / follow the system, as a menu's items (ThemeMenu, MoreMenu). The tick shows the stored
  // preference (userPrefersMode), never the resolved theme (mode): someone who follows the system must see
  // "System" ticked.
  import { setMode, userPrefersMode } from 'mode-watcher';
  import Sun from '@lucide/svelte/icons/sun';
  import Moon from '@lucide/svelte/icons/moon';
  import Monitor from '@lucide/svelte/icons/monitor';
  import Check from '@lucide/svelte/icons/check';
  import { tr } from '$lib/i18n/index.svelte';
  import { menusWords } from '$lib/i18n/menus';
  import { menuItem } from '$lib/ui';

  const t = $derived(tr(menusWords));
  const options = [
    { value: 'light', icon: Sun },
    { value: 'dark', icon: Moon },
    { value: 'system', icon: Monitor },
  ] as const;
</script>

{#each options as option (option.value)}
  {@const Icon = option.icon}
  <button
    type="button"
    role="menuitemradio"
    aria-checked={userPrefersMode.current === option.value}
    class={menuItem}
    onclick={() => setMode(option.value)}
  >
    <Icon class="size-4 text-muted-foreground" />
    <span class="flex-1">{t[option.value]}</span>
    {#if userPrefersMode.current === option.value}<Check class="size-4 text-emphasis" />{/if}
  </button>
{/each}
