<script lang="ts">
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import OpenCard from '$lib/components/OpenCard.svelte';
  import PageGrid from '$lib/components/PageGrid.svelte';
  import PasswordDialog from '$lib/components/PasswordDialog.svelte';
  import Toolbar from '$lib/components/Toolbar.svelte';
  import { Editor, setEditor } from '$lib/editor.svelte';

  const editor = setEditor(new Editor());

  /** Keyboard shortcuts on the pages: Delete, Ctrl/Cmd+Z, Ctrl/Cmd+A — not while typing. */
  function shortcut(e: KeyboardEvent) {
    if (!editor.name || editor.busy || e.target instanceof HTMLInputElement) return;
    const command = e.ctrlKey || e.metaKey;
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

{#if !editor.name}
  <OpenCard />
{:else}
  <div class="flex h-dvh flex-col">
    <Toolbar />
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
    <main class="min-h-0 flex-1 overflow-y-auto bg-muted">
      <PageGrid />
    </main>
  </div>
{/if}
