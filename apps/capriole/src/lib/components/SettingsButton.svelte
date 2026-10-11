<script lang="ts">
  // Opens the settings (SettingsDialog), beside the language's and the theme's menus.
  import Settings from '@lucide/svelte/icons/settings';
  import { getEditor } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { settingsWords } from '$lib/i18n/settings';

  const editor = getEditor();
  const t = $derived(tr(settingsWords));
</script>

<button
  type="button"
  class="flex size-9 items-center justify-center rounded-md border hover:bg-muted"
  aria-label={t.open}
  title={t.open}
  onclick={() => (editor.settingsOpen = true)}
>
  <Settings class="size-4" />
</button>
