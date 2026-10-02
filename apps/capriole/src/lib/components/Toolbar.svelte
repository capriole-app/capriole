<script lang="ts">
  // The open file and what can be done to its pages. Actions on pages apply to the selection.
  import ArrowLeft from '@lucide/svelte/icons/arrow-left';
  import ArrowRight from '@lucide/svelte/icons/arrow-right';
  import Copy from '@lucide/svelte/icons/copy';
  import FileDown from '@lucide/svelte/icons/file-down';
  import FileOutput from '@lucide/svelte/icons/file-output';
  import FilePlus from '@lucide/svelte/icons/file-plus';
  import RotateCcw from '@lucide/svelte/icons/rotate-ccw';
  import RotateCw from '@lucide/svelte/icons/rotate-cw';
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import Undo2 from '@lucide/svelte/icons/undo-2';
  import X from '@lucide/svelte/icons/x';
  import ThemeMenu from './ThemeMenu.svelte';
  import { getEditor } from '$lib/editor.svelte';
  import { fromBrowser, isDesktop } from '$lib/platform';

  const editor = getEditor();
  let input: HTMLInputElement | undefined = $state();

  const some = $derived(editor.selected.size > 0);
  const all = $derived(editor.selected.size === editor.pages);
  const button =
    'flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm hover:bg-muted disabled:pointer-events-none disabled:opacity-50';
</script>

<header class="flex flex-wrap items-center gap-2 border-b bg-card px-4 py-2">
  <img src="/logo-light.svg" alt="Capriole" class="h-7 dark:hidden" />
  <img src="/logo-dark.svg" alt="Capriole" class="hidden h-7 dark:block" />
  <div class="me-2 min-w-0 flex-1">
    <p class="truncate text-sm font-medium" title={editor.name ?? ''}>
      {editor.name}{editor.modified ? ' •' : ''}
    </p>
    <p class="text-xs text-muted-foreground" aria-live="polite">
      {editor.pages}
      {editor.pages === 1 ? 'pagina' : 'pagine'}{some ? ` · ${editor.selected.size} selezionate` : ''}
      {#if editor.busy}· al lavoro…{/if}
    </p>
  </div>

  <div class="flex flex-wrap items-center gap-2" role="toolbar" aria-label="Pagine selezionate">
    <button type="button" class={button} disabled={!some || editor.busy} onclick={() => editor.rotate(-90)}>
      <RotateCcw class="size-4" /> Ruota a sinistra
    </button>
    <button type="button" class={button} disabled={!some || editor.busy} onclick={() => editor.rotate(90)}>
      <RotateCw class="size-4" /> Ruota a destra
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.shift(-1)}
      aria-label="Sposta prima"
      title="Sposta prima"
    >
      <ArrowLeft class="size-4" />
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.shift(1)}
      aria-label="Sposta dopo"
      title="Sposta dopo"
    >
      <ArrowRight class="size-4" />
    </button>
    <button type="button" class={button} disabled={!some || editor.busy} onclick={() => editor.duplicate()}>
      <Copy class="size-4" /> Duplica
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || all || editor.busy}
      title={all ? 'Un documento conserva almeno una pagina' : undefined}
      onclick={() => editor.remove()}
    >
      <Trash2 class="size-4" /> Elimina
    </button>
    <button type="button" class={button} disabled={!some || editor.busy} onclick={() => editor.extract()}>
      <FileOutput class="size-4" /> Estrai
    </button>
    <button
      type="button"
      class={button}
      disabled={editor.busy}
      onclick={() => (isDesktop ? editor.insertFromDialog() : input?.click())}
    >
      <FilePlus class="size-4" /> Inserisci PDF
    </button>
    <input
      bind:this={input}
      type="file"
      accept="application/pdf,.pdf"
      class="hidden"
      data-testid="insert-file"
      onchange={(e) => {
        const file = e.currentTarget.files?.[0];
        if (file) fromBrowser(file).then((picked) => editor.insert(picked));
        e.currentTarget.value = '';
      }}
    />
    <button type="button" class={button} disabled={!editor.canUndo || editor.busy} onclick={() => editor.undo()}>
      <Undo2 class="size-4" /> Annulla
    </button>
    <button
      type="button"
      class="flex items-center gap-1.5 rounded-md bg-primary px-3 py-1.5 text-sm font-medium text-primary-foreground disabled:opacity-60"
      disabled={editor.busy}
      onclick={() => editor.save()}
      title={editor.path ? `Sovrascrive ${editor.path}` : undefined}
    >
      <FileDown class="size-4" /> Salva
    </button>
    {#if isDesktop}
      <button type="button" class={button} disabled={editor.busy} onclick={() => editor.save(true)}>
        <FileDown class="size-4" /> Salva con nome
      </button>
    {/if}
  </div>
  <ThemeMenu />
  <button
    type="button"
    class="flex size-9 items-center justify-center rounded-md border hover:bg-muted"
    aria-label="Chiudi il documento"
    onclick={() => {
      if (!editor.modified || confirm('Chiudere senza salvare le modifiche?')) editor.close();
    }}
  >
    <X class="size-4" />
  </button>
</header>
