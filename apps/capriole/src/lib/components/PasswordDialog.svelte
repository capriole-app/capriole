<script lang="ts">
  import Lock from '@lucide/svelte/icons/lock';
  import { getEditor } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { passwordDialogWords } from '$lib/i18n/passwordDialog';

  const editor = getEditor();
  const t = $derived(tr(passwordDialogWords));
  let password = $state('');

  // A modal dialog while a password is needed (showModal: focus trapped, Escape cancels).
  function modal(dialog: HTMLDialogElement) {
    if (editor.passwordNeeded && !dialog.open) dialog.showModal();
    if (!editor.passwordNeeded && dialog.open) dialog.close();
  }
</script>

<dialog
  {@attach modal}
  class="m-auto w-full max-w-sm rounded-xl border bg-card p-6 text-foreground shadow-lg backdrop:bg-black/40"
  oncancel={() => editor.cancelPassword()}
>
  <form
    class="flex flex-col gap-4"
    onsubmit={(e) => {
      e.preventDefault();
      editor.submitPassword(password);
    }}
  >
    <h2 class="flex items-center gap-2 font-semibold"><Lock class="size-4" /> {t.title}</h2>
    <p class="text-sm text-muted-foreground">{t.explain}</p>
    {#if editor.passwordRejected}
      <p class="rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">{t.rejected}</p>
    {/if}
    <!-- svelte-ignore a11y_autofocus -->
    <input
      type="password"
      class="rounded-md border bg-background px-3 py-2 text-base"
      aria-label={t.password}
      autocomplete="off"
      autofocus
      bind:value={password}
    />
    <div class="flex justify-end gap-2">
      <button
        type="button"
        class="rounded-md border px-3 py-1.5 text-sm hover:bg-muted"
        onclick={() => editor.cancelPassword()}>{t.cancel}</button
      >
      <button
        type="submit"
        class="rounded-md bg-primary px-3 py-1.5 text-sm font-medium text-primary-foreground"
        disabled={editor.busy}>{t.open}</button
      >
    </div>
  </form>
</dialog>
