<script lang="ts">
  // «Scarica l'app per il computer»: the desktop app's installers, from the latest GitHub release of the
  // repository (scripts/build_release.sh names them without a version, so these links never change). Plain
  // links: the page makes no request of its own (its CSP allows none to GitHub). Only in the web app.
  import Download from '@lucide/svelte/icons/download';
  import { getEditor } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { downloadWords } from '$lib/i18n/download';
  import { dialog, dialogFooter, dialogTitle, secondaryButton } from '$lib/ui';

  const releases = 'https://github.com/capriole-app/capriole/releases';
  const latest = (file: string) => `${releases}/latest/download/${file}`;

  const editor = getEditor();
  const t = $derived(tr(downloadWords));

  function modal(dialog: HTMLDialogElement) {
    if (editor.downloadOpen && !dialog.open) dialog.showModal();
    if (!editor.downloadOpen && dialog.open) dialog.close();
  }
</script>

{#snippet file(href: string, label: string, detail: string)}
  <a
    {href}
    class="flex items-center gap-3 rounded-md border px-3 py-2 hover:bg-muted"
    data-testid="download-link"
    download
  >
    <Download class="size-4 shrink-0" />
    <span class="flex flex-col">
      <span class="font-medium">{label}</span>
      <span class="text-xs text-muted-foreground">{detail}</span>
    </span>
  </a>
{/snippet}

<dialog
  {@attach modal}
  class={[dialog, 'max-w-lg']}
  aria-labelledby="download-title"
  data-testid="download-dialog"
  oncancel={(e) => {
    e.preventDefault();
    editor.downloadOpen = false;
  }}
>
  <div class="flex flex-col gap-4 text-sm">
    <h2 id="download-title" class={dialogTitle}>
      <Download class="size-4" />
      {t.title}
    </h2>
    <p>{t.intro}</p>

    <section class="flex flex-col gap-2">
      <h3 class="font-medium">{t.windows}</h3>
      {@render file(latest('Capriole_x64-setup.exe'), 'Capriole_x64-setup.exe', t.windowsFile)}
      <p class="rounded-md bg-muted px-3 py-2 text-xs text-muted-foreground">{t.unsigned}</p>
    </section>

    <section class="flex flex-col gap-2">
      <h3 class="font-medium">{t.linux}</h3>
      {@render file(latest('Capriole_amd64.deb'), 'Capriole_amd64.deb', t.deb)}
      {@render file(latest('Capriole_amd64.AppImage'), 'Capriole_amd64.AppImage', t.appImage)}
    </section>

    <p class="text-xs">
      <a href={latest('SHA256SUMS')} class="underline underline-offset-2 hover:text-emphasis">{t.checksums}</a>
      ·
      <a
        href={releases}
        target="_blank"
        rel="noopener noreferrer"
        class="underline underline-offset-2 hover:text-emphasis">{t.allReleases}</a
      >
    </p>

    <div class={dialogFooter}>
      <button type="button" class={secondaryButton} onclick={() => (editor.downloadOpen = false)}>{t.close}</button>
    </div>
  </div>
</dialog>
