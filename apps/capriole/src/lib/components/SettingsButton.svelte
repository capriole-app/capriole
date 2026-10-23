<script lang="ts">
  // Opens the settings (SettingsDialog), beside the language's and the theme's menus.
  import Settings from '@lucide/svelte/icons/settings';
  import { getApp } from '$lib/editor-context';
  import { barButton } from '$lib/ui';
  import { tr } from '$lib/i18n/index.svelte';
  import { settingsWords } from '$lib/i18n/settings';

  // On the first screen: rendered before the app has loaded too (lib/editor-context.ts).
  const app = getApp();
  const t = $derived(tr(settingsWords));
</script>

<button
  type="button"
  class={barButton}
  aria-label={t.open}
  title={t.open}
  onclick={() => {
    if (app.editor) app.editor.settingsOpen = true;
  }}
>
  <Settings class="size-4" />
</button>
