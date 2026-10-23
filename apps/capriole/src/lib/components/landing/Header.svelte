<script lang="ts">
  // The bar over the landing: the logo, the sections, and the buttons the first screen always had (report,
  // settings, language, theme). No call to action: the card in the hero is the one. The logo drawn in the
  // page (OpenCard says why it is not the file). On the web only.
  import LanguageMenu from '../LanguageMenu.svelte';
  import ReportButton from '../ReportButton.svelte';
  import SettingsButton from '../SettingsButton.svelte';
  import ThemeMenu from '../ThemeMenu.svelte';
  import logoLight from '../../../../static/logo-light.svg?raw';
  import logoDark from '../../../../static/logo-dark.svg?raw';
  import { tr } from '$lib/i18n/index.svelte';
  import { landingWords } from '$lib/i18n/landing';

  const t = $derived(tr(landingWords));
  const sections = $derived([
    ['privacy', t.nav.privacy],
    ['from-scratch', t.nav.scratch],
    ['features', t.nav.features],
    ['faq', t.nav.faq],
    ['desktop', t.nav.desktop],
  ]);
</script>

<header class="sticky top-0 z-30 border-b bg-background/90 backdrop-blur-md" data-web-only>
  <div class="mx-auto flex h-16 max-w-6xl items-center gap-6 px-4 sm:px-8">
    <a href="#top" class="flex h-8 shrink-0 items-center" aria-label="Capriole">
      <span class="h-full dark:hidden [&>svg]:h-full [&>svg]:w-auto" aria-hidden="true">{@html logoLight}</span>
      <span class="hidden h-full dark:block [&>svg]:h-full [&>svg]:w-auto" aria-hidden="true">{@html logoDark}</span>
    </a>
    <nav aria-label={t.sections} class="ms-auto hidden gap-6 text-[0.9rem] lg:flex">
      {#each sections as [id, label] (id)}
        <a href="#{id}" class="hover:text-emphasis">{label}</a>
      {/each}
    </nav>
    <div class="ms-auto flex gap-1 lg:ms-2"><ReportButton /><SettingsButton /><LanguageMenu /><ThemeMenu /></div>
  </div>
</header>
