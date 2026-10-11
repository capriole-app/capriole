<script lang="ts">
  // The first screen: a card to drop or choose a PDF. Nothing leaves the browser.
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import LanguageMenu from './LanguageMenu.svelte';
  import SettingsButton from './SettingsButton.svelte';
  import ThemeMenu from './ThemeMenu.svelte';
  import { getEditor } from '$lib/editor.svelte';
  import { choosesWhere, isDesktop } from '$lib/platform';
  import { tr } from '$lib/i18n/index.svelte';
  import { openCardWords } from '$lib/i18n/openCard';

  const editor = getEditor();
  const t = $derived(tr(openCardWords));
  let dragging = $state(false);
  let input: HTMLInputElement | undefined = $state();

  function take(files: FileList | null | undefined): void {
    const file = files?.[0];
    if (file) editor.openFile(file);
  }
</script>

<main class="flex min-h-dvh items-center justify-center bg-muted px-6 py-10">
  <div
    role="region"
    aria-label={t.region}
    class={[
      'relative w-full max-w-md rounded-xl border bg-card p-6 shadow-sm sm:p-8',
      dragging && 'ring-2 ring-primary',
    ]}
    ondragover={(e) => {
      e.preventDefault();
      dragging = true;
    }}
    ondragleave={() => (dragging = false)}
    ondrop={(e) => {
      e.preventDefault();
      dragging = false;
      take(e.dataTransfer?.files);
    }}
  >
    <div class="absolute end-3 top-3 flex gap-2"><SettingsButton /><LanguageMenu /><ThemeMenu /></div>
    <header class="mt-10 mb-8 flex flex-col items-center gap-3 text-center">
      <img src="/logo-light.svg" alt="Capriole" class="h-14 dark:hidden" />
      <img src="/logo-dark.svg" alt="Capriole" class="hidden h-14 dark:block" />
      <p class="text-sm text-muted-foreground">{t.tagline}</p>
    </header>

    {#if editor.error}
      <p class="mb-4 rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">{editor.error}</p>
    {/if}

    <button
      type="button"
      class="flex w-full items-center justify-center gap-2 rounded-md bg-primary px-4 py-2.5 text-base font-medium text-primary-foreground disabled:opacity-60"
      disabled={editor.busy}
      onclick={() => (choosesWhere ? editor.openFromDialog() : input?.click())}
    >
      <FolderOpen class="size-5" />
      {editor.busy ? t.opening : t.choose}
    </button>
    <input
      bind:this={input}
      type="file"
      accept="application/pdf,.pdf"
      class="hidden"
      data-testid="open-file"
      onchange={(e) => take(e.currentTarget.files)}
    />
    <p class="mt-4 text-center text-sm text-muted-foreground">
      {t.orDrop}
      {isDesktop ? t.staysDesktop : t.staysBrowser}
    </p>
    {#if editor.engineVersion}
      <p class="mt-6 text-center text-xs text-muted-foreground">
        {t.engine(editor.engineVersion)}
      </p>
    {/if}
    <p class="mt-2 text-center text-xs">
      <button
        type="button"
        class="text-muted-foreground underline underline-offset-2 hover:text-foreground"
        onclick={() => (editor.licensesOpen = true)}>{t.about}</button
      >
    </p>
  </div>
</main>
