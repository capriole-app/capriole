<script lang="ts">
  // The bar's «More» (⋯): what is not about the document — the settings, «Segnala un problema» (only in the
  // web app, as ReportButton), the language and the theme. The first screen keeps them as buttons of their own.
  import Ellipsis from '@lucide/svelte/icons/ellipsis';
  import MessageSquareWarning from '@lucide/svelte/icons/message-square-warning';
  import Settings from '@lucide/svelte/icons/settings';
  import LanguageItems from './LanguageItems.svelte';
  import ThemeItems from './ThemeItems.svelte';
  import { getEditor } from '$lib/editor.svelte';
  import { isDesktop } from '$lib/platform';
  import { tr } from '$lib/i18n/index.svelte';
  import { menusWords } from '$lib/i18n/menus';
  import { reportWords } from '$lib/i18n/report';
  import { settingsWords } from '$lib/i18n/settings';
  import { barButton, menu, menuItem, menuLabel, pressed } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(menusWords));
  const ts = $derived(tr(settingsWords));
  const tw = $derived(tr(reportWords));
  let open = $state(false);
</script>

<svelte:window onclick={() => (open = false)} onkeydown={(e) => e.key === 'Escape' && (open = false)} />

<div class="relative">
  <button
    type="button"
    class={[barButton, open && pressed]}
    aria-label={t.more}
    title={t.moreTitle}
    aria-haspopup="menu"
    aria-expanded={open}
    data-testid="more-menu"
    onclick={(e) => {
      e.stopPropagation();
      open = !open;
    }}
  >
    <Ellipsis class="size-4" />
  </button>
  {#if open}
    <div role="menu" class={[menu, 'w-56']}>
      <button type="button" role="menuitem" class={menuItem} onclick={() => (editor.settingsOpen = true)}>
        <Settings class="size-4 text-muted-foreground" />
        {ts.open}
      </button>
      {#if !isDesktop}
        <button type="button" role="menuitem" class={menuItem} onclick={() => (editor.reportOpen = true)}>
          <MessageSquareWarning class="size-4 text-muted-foreground" />
          {tw.open}
        </button>
      {/if}
      <div role="separator" class="my-1 h-px bg-border"></div>
      <div role="group" aria-labelledby="more-language">
        <p id="more-language" class={menuLabel}>{t.language}</p>
        <LanguageItems />
      </div>
      <div role="separator" class="my-1 h-px bg-border"></div>
      <div role="group" aria-labelledby="more-theme">
        <p id="more-theme" class={menuLabel}>{t.theme}</p>
        <ThemeItems />
      </div>
    </div>
  {/if}
</div>
