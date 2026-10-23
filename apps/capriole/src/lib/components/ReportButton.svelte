<script lang="ts">
  // Opens «Segnala un problema» (ReportDialog), beside the settings. Only in the web app: the desktop app
  // would need its own way to the service (deploy/reports), not made yet.
  import MessageSquareWarning from '@lucide/svelte/icons/message-square-warning';
  import { getApp } from '$lib/editor-context';
  import { barButton } from '$lib/ui';
  import { isDesktop } from '$lib/platform';
  import { tr } from '$lib/i18n/index.svelte';
  import { reportWords } from '$lib/i18n/report';

  // On the first screen: rendered before the app has loaded too (lib/editor-context.ts).
  const app = getApp();
  const t = $derived(tr(reportWords));
</script>

{#if !isDesktop}
  <button
    type="button"
    class={barButton}
    aria-label={t.open}
    title={t.open}
    data-testid="report-button"
    data-web-only
    onclick={() => {
      if (app.editor) app.editor.reportOpen = true;
    }}
  >
    <MessageSquareWarning class="size-4" />
  </button>
{/if}
