<script lang="ts">
  // «Condizioni d'uso» (Terms of use): the text, in the interface's language, with the note that the English
  // one prevails. Opened from the first screen (where they are accepted) and from «Informazioni e licenze».
  import Scale from '@lucide/svelte/icons/scale';
  import { getEditor } from '$lib/editor.svelte';
  import { i18n, tr } from '$lib/i18n/index.svelte';
  import { termsWords } from '$lib/i18n/terms';
  import { provider, termsVersion } from '$lib/terms-version';

  const editor = getEditor();
  const t = $derived(tr(termsWords));
  // The version is a calendar date: read at noon UTC, so that no time zone moves it to the day before.
  const date = $derived(
    new Date(`${termsVersion}T12:00:00Z`).toLocaleDateString(i18n.intl, { dateStyle: 'long', timeZone: 'UTC' }),
  );

  // A modal dialog while it is open (showModal: focus trapped, Escape closes).
  function modal(dialog: HTMLDialogElement) {
    if (editor.termsOpen && !dialog.open) dialog.showModal();
    if (!editor.termsOpen && dialog.open) dialog.close();
  }
</script>

<dialog
  {@attach modal}
  class="m-auto w-full max-w-2xl rounded-xl border bg-card p-6 text-foreground shadow-lg backdrop:bg-black/40"
  aria-labelledby="terms-title"
  data-testid="terms-dialog"
  oncancel={(e) => {
    e.preventDefault();
    editor.termsOpen = false;
  }}
>
  <div class="flex max-h-[80dvh] flex-col gap-4 text-sm">
    <header class="flex flex-col gap-0.5">
      <h2 id="terms-title" class="flex items-center gap-2 text-base font-semibold">
        <Scale class="size-4" />
        {t.title}
      </h2>
      <p class="text-xs text-muted-foreground">{t.updated(date)}</p>
    </header>
    <div class="flex min-h-0 flex-col gap-4 overflow-y-auto pe-1">
      {#each t.sections(provider) as section (section.heading)}
        <section class="flex flex-col gap-1.5">
          <h3 class="font-medium">{section.heading}</h3>
          {#each section.body as paragraph, i (i)}
            <p>{paragraph}</p>
          {/each}
        </section>
      {/each}
      <p class="text-xs text-muted-foreground italic">{t.prevails}</p>
    </div>
    <div class="flex justify-end">
      <button
        type="button"
        class="rounded-md border px-3 py-1.5 hover:bg-muted"
        onclick={() => (editor.termsOpen = false)}>{t.close}</button
      >
    </div>
  </div>
</dialog>
