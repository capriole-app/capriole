<script lang="ts">
  // The landing's foot: who makes Capriole, its licences and terms (the dialogs of the app, once loaded),
  // the code, the address to write to. On the web only: in the shell the card has its own links.
  import CodedInIreland from '../CodedInIreland.svelte';
  import { getApp } from '$lib/editor-context';
  import { tr } from '$lib/i18n/index.svelte';
  import { openCardWords } from '$lib/i18n/openCard';
  import { termsWords } from '$lib/i18n/terms';
  import { repository } from '$lib/releases';

  const app = getApp();
  const t = $derived(tr(openCardWords));
  const tt = $derived(tr(termsWords));
  const link = 'hover:text-foreground';
</script>

<footer class="border-t bg-card py-10 text-sm text-muted-foreground" data-web-only>
  <div class="mx-auto flex max-w-6xl flex-wrap items-center gap-x-7 gap-y-4 px-4 sm:px-8">
    <span class="font-serif text-lg font-semibold text-foreground">Capriole</span>
    <span>© Bobolar Ltd</span>
    <button
      type="button"
      class={link}
      onclick={() => {
        if (app.editor) app.editor.licensesOpen = true;
      }}>{t.about}</button
    >
    <button
      type="button"
      class={link}
      onclick={() => {
        if (app.editor) app.editor.termsOpen = true;
      }}>{tt.title}</button
    >
    <a href={repository} class={link} target="_blank" rel="noopener noreferrer">GitHub</a>
    <a href="mailto:hello@capriole.app" class={link}>hello@capriole.app</a>
    <span class="ms-auto"><CodedInIreland /></span>
  </div>
</footer>
