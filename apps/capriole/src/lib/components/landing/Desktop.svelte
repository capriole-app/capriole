<script lang="ts">
  // The desktop app: the second call to action, said once. The installers of the latest release
  // (lib/releases.ts), plain links.
  import Download from '@lucide/svelte/icons/download';
  import Heading from './Heading.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { landingWords } from '$lib/i18n/landing';
  import { downloadWords } from '$lib/i18n/download';
  import { installers, latest, releases } from '$lib/releases';

  const t = $derived(tr(landingWords).desktop);
  const td = $derived(tr(downloadWords));
  const files = $derived([
    { file: installers.windows, label: t.windows, detail: td.windows },
    { file: installers.deb, label: `${t.linux} (.deb)`, detail: td.deb },
    { file: installers.appImage, label: `${t.linux} (AppImage)`, detail: td.appImage },
  ]);
</script>

<section id="desktop" aria-labelledby="desktop-title" class="scroll-mt-16 bg-card py-20 sm:py-28">
  <div class="mx-auto grid max-w-6xl items-center gap-12 px-4 sm:px-8 lg:grid-cols-2 lg:gap-16">
    <div>
      <Heading id="desktop-title" eyebrow={t.eyebrow} title={t.title} />
      <p class="mt-5 max-w-xl text-lg text-muted-foreground">{t.lead}</p>
    </div>
    <div class="flex flex-col gap-3">
      {#each files as { file, label, detail } (file)}
        <a
          href={latest(file)}
          class="flex items-center gap-3 rounded-lg border bg-card px-4 py-3 hover:bg-muted"
          data-testid="landing-download"
          download
        >
          <Download class="size-5 shrink-0 text-emphasis" aria-hidden="true" />
          <span class="flex flex-col">
            <span class="font-medium">{label}</span>
            <span class="text-sm text-muted-foreground">{detail}</span>
          </span>
        </a>
      {/each}
      <p class="mt-1 text-sm text-muted-foreground">
        {t.mac}
        <a
          href={releases}
          target="_blank"
          rel="noopener noreferrer"
          class="underline underline-offset-2 hover:text-emphasis">{td.allReleases}</a
        >
      </p>
      <p class="text-xs text-muted-foreground">{td.unsigned}</p>
    </div>
  </div>
</section>
