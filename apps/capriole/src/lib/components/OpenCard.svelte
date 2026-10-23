<script lang="ts">
  // The card that opens a PDF: the landing's one call to action (landing/Hero.svelte), and in the desktop
  // shell the whole first screen. Nothing leaves the browser. A file opens only once the terms of use are
  // accepted (lib/terms.svelte.ts): a box to tick, the first time and when they change. A PDF dropped
  // anywhere on the page counts as dropped here; refused, the card is brought into view to say why.
  // Rendered at build time too, before there is an editor (lib/editor-context.ts): the same card, which
  // opens files once the app has loaded, a moment later.
  // Two looks from one markup, chosen by CSS (desktop:…, the mark app.html puts on <html> before anything
  // paints), never by isDesktop in the template — the build renders the web's page, and the shell hydrates
  // it: on the web a drop area in a window's frame (the header and the hero say the rest); in the shell the
  // card of before, with its logo, its buttons and its links.
  // The logo: drawn in the page until the app runs, then the file — a request for /logo-light.svg is what
  // counts a start of the app (deploy/web/Caddyfile), and robots reading the page must not ask for it. On the
  // web the card's logo is hidden, but its file is asked for all the same: the count stays as it was.
  import FileUp from '@lucide/svelte/icons/file-up';
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import CodedInIreland from './CodedInIreland.svelte';
  import LanguageMenu from './LanguageMenu.svelte';
  import ReportButton from './ReportButton.svelte';
  import SettingsButton from './SettingsButton.svelte';
  import ThemeMenu from './ThemeMenu.svelte';
  import { getApp } from '$lib/editor-context';
  import logoLight from '../../../static/logo-light.svg?raw';
  import logoDark from '../../../static/logo-dark.svg?raw';
  import { choosesWhere, isDesktop } from '$lib/platform';
  import { tr } from '$lib/i18n/index.svelte';
  import { openCardWords } from '$lib/i18n/openCard';
  import { landingWords } from '$lib/i18n/landing';
  import { termsWords } from '$lib/i18n/terms';
  import { reportWords } from '$lib/i18n/report';
  import { terms } from '$lib/terms.svelte';

  const app = getApp();
  const editor = $derived(app.editor);
  const t = $derived(tr(openCardWords));
  const tl = $derived(tr(landingWords).card);
  const tt = $derived(tr(termsWords));
  const tw = $derived(tr(reportWords));
  let ticked = $state(false);
  /** A file dropped or chosen before the terms were accepted: say what is missing. */
  let refused = $state(false);
  /** Files are being dragged over the page: dragenter and dragleave come for every element crossed. */
  let over = $state(0);
  let card: HTMLElement | undefined = $state();
  let input: HTMLInputElement | undefined = $state();

  const link = 'text-muted-foreground underline underline-offset-2 hover:text-foreground';

  function take(files: FileList | null | undefined): void {
    const file = files?.[0];
    if (!file) return;
    if (!terms.accepted) {
      refused = true;
      card?.scrollIntoView({ block: 'center', behavior: 'smooth' });
      return;
    }
    editor?.openFile(file);
  }

  /** Only files: a link or some text dragged within the page is left to the browser. */
  const files = (e: DragEvent) => e.dataTransfer?.types.includes('Files') ?? false;
</script>

<svelte:window
  ondragenter={(e) => {
    if (files(e)) over++;
  }}
  ondragleave={(e) => {
    if (files(e)) over = Math.max(0, over - 1);
  }}
  ondragover={(e) => {
    if (files(e)) e.preventDefault();
  }}
  ondrop={(e) => {
    if (!files(e)) return;
    e.preventDefault();
    over = 0;
    take(e.dataTransfer?.files);
  }}
/>

<div
  bind:this={card}
  role="region"
  aria-label={t.region}
  class={[
    'relative w-full overflow-hidden rounded-xl border bg-card shadow-[0_28px_70px_-34px_rgb(17_29_74/0.38)]',
    'desktop:max-w-md desktop:overflow-visible desktop:p-8 desktop:shadow-none',
    over > 0 && 'ring-2 ring-emphasis',
  ]}
>
  <!-- A window's frame, on the web. -->
  <div class="flex h-8 items-center gap-1.5 border-b bg-background px-3 desktop:hidden" aria-hidden="true">
    <span class="size-2.5 rounded-full bg-border"></span><span class="size-2.5 rounded-full bg-border"></span><span
      class="size-2.5 rounded-full bg-border"
    ></span>
  </div>

  <div class="hidden desktop:absolute desktop:end-3 desktop:top-3 desktop:flex desktop:gap-1">
    <ReportButton /><SettingsButton /><LanguageMenu /><ThemeMenu />
  </div>
  <header class="hidden desktop:mt-10 desktop:mb-8 desktop:flex desktop:flex-col desktop:items-center desktop:gap-3">
    {#if editor}
      <img src="/logo-light.svg" alt="Capriole" class="h-14 dark:hidden" />
      <img src="/logo-dark.svg" alt="Capriole" class="hidden h-14 dark:block" />
    {:else}
      <!-- The repository's own logo files, as they are. -->
      <span role="img" aria-label="Capriole" class="h-14 dark:hidden [&>svg]:h-full [&>svg]:w-auto"
        >{@html logoLight}</span
      >
      <span role="img" aria-label="Capriole" class="hidden h-14 dark:block [&>svg]:h-full [&>svg]:w-auto"
        >{@html logoDark}</span
      >
    {/if}
    <p class="text-center text-sm text-muted-foreground">{t.tagline}</p>
  </header>

  <div class="p-4 sm:p-6 desktop:p-0">
    {#if app.failed}
      <p class="mb-4 rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">{t.failed}</p>
    {/if}
    {#if editor?.error}
      <p
        class="mb-4 flex items-center gap-3 rounded-md bg-danger-background px-3 py-2 text-sm text-danger"
        role="alert"
      >
        <span class="flex-1">{editor.error}</span>
        {#if !isDesktop}
          <button
            type="button"
            class="shrink-0 underline underline-offset-2"
            onclick={() => {
              if (editor) editor.reportOpen = true;
            }}>{tw.reportThis}</button
          >
        {/if}
      </p>
    {/if}

    <div
      class={[
        'flex flex-col items-center rounded-xl border-2 border-dashed border-emphasis/30 bg-accent/40 px-4 py-5 text-center sm:py-8',
        'desktop:border-0 desktop:bg-transparent desktop:p-0',
      ]}
    >
      <FileUp class="hidden size-10 text-emphasis sm:block desktop:hidden" strokeWidth={1.6} aria-hidden="true" />
      <p class="mb-4 text-base font-semibold sm:mt-2 sm:mb-5 sm:text-lg desktop:hidden">{tl.drop}</p>

      {#if !terms.accepted}
        <label class="mb-4 flex items-start gap-2 text-start text-sm" data-testid="accept-terms" data-terms-box>
          <input
            type="checkbox"
            class="mt-0.5 size-4 shrink-0 accent-emphasis"
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
              class="underline underline-offset-2 hover:text-emphasis"
              onclick={(e) => {
                e.preventDefault();
                if (editor) editor.termsOpen = true;
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
        class="flex w-full max-w-xs items-center justify-center gap-2 rounded-md bg-primary px-4 py-3 text-base font-medium text-primary-foreground hover:bg-primary/90 disabled:opacity-60 desktop:max-w-none desktop:py-2.5"
        disabled={editor?.busy || !terms.accepted}
        onclick={() => {
          if (editor) void (choosesWhere ? editor.openFromDialog() : input?.click());
        }}
      >
        <FolderOpen class="size-5" />
        {editor?.busy ? t.opening : t.choose}
      </button>
      <input
        bind:this={input}
        type="file"
        accept="application/pdf,.pdf"
        class="hidden"
        data-testid="open-file"
        onchange={(e) => take(e.currentTarget.files)}
      />
    </div>

    <p class="mt-4 text-center text-sm text-muted-foreground">
      <span class="desktop:hidden">{t.staysBrowser}</span>
      <span class="hidden desktop:inline">{t.orDrop} {t.staysDesktop}</span>
    </p>
    {#if editor?.engineVersion}
      <p class="mt-3 text-center text-xs text-muted-foreground desktop:mt-6">
        {t.engine(editor.engineVersion)}
      </p>
    {/if}

    <!-- The shell's links; on the web the footer has them. -->
    <p class="hidden text-center text-xs desktop:mt-2 desktop:block">
      <button
        type="button"
        class={link}
        onclick={() => {
          if (editor) editor.licensesOpen = true;
        }}>{t.about}</button
      >
      ·
      <button
        type="button"
        class={link}
        onclick={() => {
          if (editor) editor.termsOpen = true;
        }}>{tt.title}</button
      >
    </p>
    <p class="mt-2 text-center text-xs" data-web-only>
      <button
        type="button"
        class={link}
        data-testid="download-open"
        onclick={() => {
          if (editor) editor.downloadOpen = true;
        }}>{tl.desktop}</button
      >
    </p>
    <div class="hidden desktop:mt-5 desktop:flex desktop:justify-center"><CodedInIreland /></div>
  </div>
</div>
