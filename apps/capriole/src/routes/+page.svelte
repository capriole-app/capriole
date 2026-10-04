<script lang="ts">
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import OpenCard from '$lib/components/OpenCard.svelte';
  import AnnotationToolbar from '$lib/components/AnnotationToolbar.svelte';
  import AttachmentsPanel from '$lib/components/AttachmentsPanel.svelte';
  import PageGrid from '$lib/components/PageGrid.svelte';
  import PageView from '$lib/components/PageView.svelte';
  import PasswordDialog from '$lib/components/PasswordDialog.svelte';
  import PdfaDialog from '$lib/components/PdfaDialog.svelte';
  import SignaturesPanel from '$lib/components/SignaturesPanel.svelte';
  import SignDialog from '$lib/components/SignDialog.svelte';
  import Toolbar from '$lib/components/Toolbar.svelte';
  import { Editor, setEditor } from '$lib/editor.svelte';

  const editor = setEditor(new Editor());

  /**
   * Keyboard shortcuts — not while typing. On the pages: Delete, Ctrl/Cmd+Z, Ctrl/Cmd+A. On a page shown
   * large: Delete removes the selected annotation, Escape lets it go, then goes back to the pages.
   */
  function shortcut(e: KeyboardEvent) {
    const typing =
      e.target instanceof HTMLInputElement ||
      e.target instanceof HTMLTextAreaElement ||
      e.target instanceof HTMLSelectElement ||
      (e.target instanceof HTMLElement && e.target.isContentEditable);
    if (!editor.name || editor.busy || typing || editor.typing || editor.signRequest || editor.pdfaDialog !== null)
      return;
    const command = e.ctrlKey || e.metaKey;
    if (editor.viewing !== null) {
      const selected = editor.selectedAnnotation;
      if ((e.key === 'Delete' || e.key === 'Backspace') && selected) {
        e.preventDefault();
        editor.removeAnnotation(selected);
      } else if (e.key === 'Escape') {
        if (selected) editor.selectedAnnotation = null;
        else editor.closeView();
      } else if (command && e.key.toLowerCase() === 'z') {
        e.preventDefault();
        editor.undo();
      }
      return;
    }
    if ((e.key === 'Delete' || e.key === 'Backspace') && editor.selected.size > 0) {
      e.preventDefault();
      editor.remove();
    } else if (command && e.key.toLowerCase() === 'z') {
      e.preventDefault();
      editor.undo();
    } else if (command && e.key.toLowerCase() === 'a') {
      e.preventDefault();
      editor.selectAll();
    }
  }
</script>

<svelte:window
  onkeydown={shortcut}
  onbeforeunload={(e) => {
    if (editor.modified) e.preventDefault();
  }}
/>

<PasswordDialog />
<SignDialog />
<PdfaDialog />

{#if !editor.name}
  <OpenCard />
{:else}
  <div class="flex h-dvh flex-col">
    {#if editor.viewing !== null}
      <AnnotationToolbar />
    {:else}
      <Toolbar />
    {/if}
    {#if editor.error}
      <p class="bg-danger-background px-4 py-2 text-sm text-danger" role="alert">{editor.error}</p>
    {/if}
    {#if editor.repairs.length > 0}
      <details class="bg-warning-background px-4 py-2 text-sm text-warning">
        <summary class="flex cursor-pointer items-center gap-2">
          <TriangleAlert class="size-4" /> Il file era danneggiato: il motore lo ha riparato ({editor.repairs.length}
          {editor.repairs.length === 1 ? 'riparazione' : 'riparazioni'}). Salvando, si salva la versione riparata.
        </summary>
        <ul class="mt-2 list-disc ps-6 text-xs">
          {#each editor.repairs as repair, i (i)}
            <li>{repair.description}</li>
          {/each}
        </ul>
      </details>
    {/if}
    {#if editor.viewing !== null}
      <PageView />
    {:else}
      <div class="flex min-h-0 flex-1">
        <main class="min-h-0 min-w-0 flex-1 overflow-y-auto bg-muted">
          <PageGrid />
        </main>
        {#if editor.attachmentsOpen}
          <AttachmentsPanel />
        {:else if editor.signaturesOpen}
          <SignaturesPanel />
        {/if}
      </div>
    {/if}
  </div>
{/if}
