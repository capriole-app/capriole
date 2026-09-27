<script lang="ts">
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import FileDown from '@lucide/svelte/icons/file-down';
  import X from '@lucide/svelte/icons/x';
  import ThemeMenu from './ThemeMenu.svelte';
  import { getInspector } from '$lib/inspector.svelte';
  import { formatBytes, pdfDateNow } from '$lib/pdf/text';

  const inspector = getInspector();
  let input: HTMLInputElement | undefined = $state();
  const repaired = $derived((inspector.summary?.repairs.length ?? 0) > 0);
</script>

<header class="flex flex-wrap items-center gap-3 border-b bg-card px-4 py-2">
  <img src="/logo-light.svg" alt="Capriole" class="h-7 dark:hidden" />
  <img src="/logo-dark.svg" alt="Capriole" class="hidden h-7 dark:block" />
  <div class="min-w-0 flex-1">
    <p class="truncate text-sm font-medium" title={inspector.file?.name}>{inspector.file?.name}</p>
    <p class="text-xs text-muted-foreground">
      {inspector.file ? formatBytes(inspector.file.size) : ''}
      {#if inspector.strict}· lettura rigorosa{/if}
      {#if inspector.engineVersion}· motore {inspector.engineVersion}{/if}
    </p>
  </div>
  <button
    type="button"
    class="flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm hover:bg-muted"
    disabled={inspector.busy}
    onclick={() => inspector.save(false, null)}
    title="Riscrive tutto il file (con le riparazioni applicate)"
  >
    <FileDown class="size-4" /> Salvataggio completo
  </button>
  <button
    type="button"
    class="flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm hover:bg-muted disabled:opacity-50"
    disabled={inspector.busy || repaired}
    onclick={() => inspector.save(true, pdfDateNow())}
    title={repaired
      ? 'Non possibile: il file ha richiesto riparazioni'
      : 'Appende un aggiornamento (nuova /ModDate) ai byte originali, che restano identici'}
  >
    <FileDown class="size-4" /> Incrementale
  </button>
  <button
    type="button"
    class="flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm hover:bg-muted"
    onclick={() => input?.click()}
  >
    <FolderOpen class="size-4" /> Apri
  </button>
  <input
    bind:this={input}
    type="file"
    accept="application/pdf,.pdf"
    class="hidden"
    onchange={(e) => {
      const file = e.currentTarget.files?.[0];
      if (file) inspector.open(file);
    }}
  />
  <ThemeMenu />
  <button
    type="button"
    class="flex size-9 items-center justify-center rounded-md border hover:bg-muted"
    aria-label="Chiudi il documento"
    onclick={() => inspector.close()}
  >
    <X class="size-4" />
  </button>
</header>
