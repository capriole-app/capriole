<script lang="ts">
  // The first screen: a card to drop or choose a PDF. Nothing leaves the browser. A file opens only once
  // the terms of use are accepted (lib/terms.svelte.ts): a box to tick, the first time and when they change.
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import CodedInIreland from './CodedInIreland.svelte';
  import LanguageMenu from './LanguageMenu.svelte';
  import ReportButton from './ReportButton.svelte';
  import SettingsButton from './SettingsButton.svelte';
  import ThemeMenu from './ThemeMenu.svelte';
  import { getEditor } from '$lib/editor.svelte';
  import { choosesWhere, isDesktop } from '$lib/platform';
  import { tr } from '$lib/i18n/index.svelte';
  import { openCardWords } from '$lib/i18n/openCard';
  import { termsWords } from '$lib/i18n/terms';
  import { reportWords } from '$lib/i18n/report';
  import { downloadWords } from '$lib/i18n/download';
  import { terms } from '$lib/terms.svelte';

  const editor = getEditor();
  const t = $derived(tr(openCardWords));
  const tt = $derived(tr(termsWords));
  const tw = $derived(tr(reportWords));
  const td = $derived(tr(downloadWords));
  let ticked = $state(false);
  /** A file dropped or chosen before the terms were accepted: say what is missing. */
  let refused = $state(false);
  let dragging = $state(false);
  let input: HTMLInputElement | undefined = $state();

  function take(files: FileList | null | undefined): void {
    const file = files?.[0];
    if (!file) return;
    if (!terms.accepted) {
      refused = true;
      return;
    }
    editor.openFile(file);
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
    <div class="absolute end-3 top-3 flex gap-2"><ReportButton /><SettingsButton /><LanguageMenu /><ThemeMenu /></div>
    <header class="mt-10 mb-8 flex flex-col items-center gap-3 text-center">
      <img src="/logo-light.svg" alt="Capriole" class="h-14 dark:hidden" />
      <img src="/logo-dark.svg" alt="Capriole" class="hidden h-14 dark:block" />
      <p class="text-sm text-muted-foreground">{t.tagline}</p>
    </header>

    {#if editor.error}
      <p
        class="mb-4 flex items-center gap-3 rounded-md bg-danger-background px-3 py-2 text-sm text-danger"
        role="alert"
      >
        <span class="flex-1">{editor.error}</span>
        {#if !isDesktop}
          <button type="button" class="shrink-0 underline underline-offset-2" onclick={() => (editor.reportOpen = true)}
            >{tw.reportThis}</button
          >
        {/if}
      </p>
    {/if}

    {#if !terms.accepted}
      <label class="mb-4 flex items-start gap-2 text-sm" data-testid="accept-terms">
        <input
          type="checkbox"
          class="mt-0.5 size-4 shrink-0 accent-primary"
          bind:checked={ticked}
          onchange={() => {
            if (ticked) {
              terms.accept();
              refused = false;
            }
          }}
        />
        <span>
          {tt.acceptBefore}
          <button
            type="button"
            class="underline underline-offset-2 hover:text-primary"
            onclick={(e) => {
              e.preventDefault();
              editor.termsOpen = true;
            }}>{tt.acceptLink}</button
          >{tt.acceptAfter ? ` ${tt.acceptAfter}` : ''}.
        </span>
      </label>
      {#if refused}
        <p class="mb-4 rounded-md bg-warning-background px-3 py-2 text-sm text-warning" role="alert">
          {tt.mustAccept}
        </p>
      {/if}
    {/if}

    <button
      type="button"
      class="flex w-full items-center justify-center gap-2 rounded-md bg-primary px-4 py-2.5 text-base font-medium text-primary-foreground disabled:opacity-60"
      disabled={editor.busy || !terms.accepted}
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
      ·
      <button
        type="button"
        class="text-muted-foreground underline underline-offset-2 hover:text-foreground"
        onclick={() => (editor.termsOpen = true)}>{tt.title}</button
      >
    </p>
    {#if !isDesktop}
      <p class="mt-2 text-center text-xs">
        <button
          type="button"
          class="text-muted-foreground underline underline-offset-2 hover:text-foreground"
          data-testid="download-open"
          onclick={() => (editor.downloadOpen = true)}>{td.open}</button
        >
      </p>
    {/if}
    <div class="mt-5 flex justify-center"><CodedInIreland /></div>
  </div>
</main>
