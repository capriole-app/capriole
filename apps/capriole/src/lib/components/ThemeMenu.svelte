<script lang="ts">
  // Light / dark / follow the system. The tick shows the stored preference (userPrefersMode), never
  // the resolved theme (mode): someone who follows the system must see "System" ticked.
  import { setMode, userPrefersMode } from 'mode-watcher';
  import Sun from '@lucide/svelte/icons/sun';
  import Moon from '@lucide/svelte/icons/moon';
  import Monitor from '@lucide/svelte/icons/monitor';
  import Check from '@lucide/svelte/icons/check';
  import { tr } from '$lib/i18n/index.svelte';
  import { menusWords } from '$lib/i18n/menus';

  const t = $derived(tr(menusWords));
  const options = [
    { value: 'light', icon: Sun },
    { value: 'dark', icon: Moon },
    { value: 'system', icon: Monitor },
  ] as const;

  let open = $state(false);
</script>

<svelte:window onclick={() => (open = false)} onkeydown={(e) => e.key === 'Escape' && (open = false)} />

<div class="relative">
  <button
    type="button"
    class="flex size-9 items-center justify-center rounded-md border hover:bg-muted"
    aria-label={t.theme}
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
    <div role="menu" class="absolute end-0 z-20 mt-1 w-36 rounded-md border bg-card p-1 shadow-md">
      {#each options as option (option.value)}
        {@const Icon = option.icon}
        <button
          type="button"
          role="menuitemradio"
          aria-checked={userPrefersMode.current === option.value}
          class="flex w-full items-center gap-2 rounded px-2 py-1.5 text-sm hover:bg-muted"
          onclick={() => setMode(option.value)}
        >
          <Icon class="size-4" />
          <span class="flex-1 text-start">{t[option.value]}</span>
          {#if userPrefersMode.current === option.value}<Check class="size-4" />{/if}
        </button>
      {/each}
    </div>
  {/if}
</div>
