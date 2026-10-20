<script lang="ts">
  import Lock from '@lucide/svelte/icons/lock';
  import { getEditor } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { passwordDialogWords } from '$lib/i18n/passwordDialog';
  import { dialog, dialogFooter, field, primaryButton, secondaryButton } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(passwordDialogWords));
  let password = $state('');

  // A modal dialog while a password is needed (showModal: focus trapped, Escape cancels).
  function modal(dialog: HTMLDialogElement) {
    if (editor.passwordNeeded && !dialog.open) dialog.showModal();
    if (!editor.passwordNeeded && dialog.open) dialog.close();
  }
</script>

<dialog {@attach modal} class={[dialog, 'max-w-sm']} oncancel={() => editor.cancelPassword()}>
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
    <input type="password" class={field} aria-label={t.password} autocomplete="off" autofocus bind:value={password} />
    <div class={dialogFooter}>
      <button type="button" class={secondaryButton} onclick={() => editor.cancelPassword()}>{t.cancel}</button>
      <button type="submit" class={primaryButton} disabled={editor.busy}>{t.open}</button>
    </div>
  </form>
</dialog>
