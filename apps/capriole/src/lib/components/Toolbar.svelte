<script lang="ts">
  // The open file and what can be done to its pages. Actions on pages apply to the selection.
  import Archive from '@lucide/svelte/icons/archive';
  import ArrowLeft from '@lucide/svelte/icons/arrow-left';
  import ArrowRight from '@lucide/svelte/icons/arrow-right';
  import Copy from '@lucide/svelte/icons/copy';
  import FileDown from '@lucide/svelte/icons/file-down';
  import FileOutput from '@lucide/svelte/icons/file-output';
  import FilePlus from '@lucide/svelte/icons/file-plus';
  import Highlighter from '@lucide/svelte/icons/highlighter';
  import Paperclip from '@lucide/svelte/icons/paperclip';
  import ReceiptText from '@lucide/svelte/icons/receipt-text';
  import RotateCcw from '@lucide/svelte/icons/rotate-ccw';
  import RotateCw from '@lucide/svelte/icons/rotate-cw';
  import Signature from '@lucide/svelte/icons/signature';
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import Undo2 from '@lucide/svelte/icons/undo-2';
  import X from '@lucide/svelte/icons/x';
  import ThemeMenu from './ThemeMenu.svelte';
  import { getEditor } from '$lib/editor.svelte';
  import { choosesWhere, fromBrowser } from '$lib/platform';

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
    <!-- One line, truncated: the bar keeps its height whatever the selection says (a drag must not move the grid). -->
    <p class="truncate text-xs text-muted-foreground" aria-live="polite">
      {editor.pages}
      {editor.pages === 1 ? 'pagina' : 'pagine'}{some ? ` · ${editor.selected.size} selezionate` : ''}
      {#if editor.busy}· al lavoro…{/if}
    </p>
  </div>

  <div class="flex flex-wrap items-center gap-2" role="toolbar" aria-label="Pagine selezionate">
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.rotate(-90)}
      title="Ruota a sinistra"
    >
      <RotateCcw class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Ruota a sinistra</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.rotate(90)}
      title="Ruota a destra"
    >
      <RotateCw class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Ruota a destra</span>
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
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.duplicate()}
      title="Duplica"
    >
      <Copy class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Duplica</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || all || editor.busy}
      title={all ? 'Un documento conserva almeno una pagina' : undefined}
      onclick={() => editor.remove()}
    >
      <Trash2 class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Elimina</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={editor.selected.size !== 1 || editor.busy}
      title="Mostra la pagina grande per annotarla (anche: doppio clic)"
      onclick={() => editor.view(editor.selection[0]!)}
    >
      <Highlighter class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Annota</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={!some || editor.busy}
      onclick={() => editor.extract()}
      title="Estrai"
    >
      <FileOutput class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Estrai</span>
    </button>
    <button
      type="button"
      class={button}
      disabled={editor.busy}
      onclick={() => (choosesWhere ? editor.insertFromDialog() : input?.click())}
      title="Inserisci PDF"
    >
      <FilePlus class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Inserisci PDF</span>
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
    <button
      type="button"
      class={[button, editor.attachmentsOpen && 'border-primary bg-accent']}
      aria-pressed={editor.attachmentsOpen}
      title={editor.facturx ? 'Fattura elettronica Factur-X e allegati' : 'Allegati del documento'}
      onclick={() => {
        editor.attachmentsOpen = !editor.attachmentsOpen;
        if (editor.attachmentsOpen) editor.signaturesOpen = false;
      }}
    >
      {#if editor.facturx}
        <ReceiptText class="size-4" />
      {:else}
        <Paperclip class="size-4" />
      {/if}
      <span class="sr-only min-[1900px]:not-sr-only"
        >Allegati{editor.attachments.length > 0 ? ` (${editor.attachments.length})` : ''}</span
      >
      {#if editor.attachments.length > 0}<span class="min-[1900px]:hidden" aria-hidden="true"
          >{editor.attachments.length}</span
        >{/if}
    </button>
    <button
      type="button"
      class={button}
      disabled={editor.busy}
      title={editor.pdfa ? `Il documento è PDF/A-${editor.pdfa}` : 'Converti in PDF/A (formato d’archivio)'}
      onclick={() => editor.openPdfa()}
    >
      <Archive class="size-4" />
      <span class="sr-only min-[1900px]:not-sr-only">{editor.pdfa ? `PDF/A-${editor.pdfa}` : 'PDF/A'}</span>
    </button>
    <button
      type="button"
      class={[button, editor.signaturesOpen && 'border-primary bg-accent']}
      aria-pressed={editor.signaturesOpen}
      title="Firme del documento, e firma"
      onclick={() => {
        editor.signaturesOpen = !editor.signaturesOpen;
        if (editor.signaturesOpen) editor.attachmentsOpen = false;
      }}
    >
      <Signature class="size-4" />
      <span class="sr-only min-[1900px]:not-sr-only"
        >Firme{editor.signatures.length > 0 ? ` (${editor.signatures.length})` : ''}</span
      >
      {#if editor.signatures.length > 0}<span class="min-[1900px]:hidden" aria-hidden="true"
          >{editor.signatures.length}</span
        >{/if}
    </button>
    <button
      type="button"
      class={button}
      disabled={!editor.canUndo || editor.busy}
      onclick={() => editor.undo()}
      title="Annulla"
    >
      <Undo2 class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Annulla</span>
    </button>
    <button
      type="button"
      class="flex items-center gap-1.5 rounded-md bg-primary px-3 py-1.5 text-sm font-medium text-primary-foreground disabled:opacity-60"
      disabled={editor.busy}
      onclick={() => editor.save()}
      title={editor.signed
        ? 'Salva aggiungendo le modifiche dopo le firme (le firme restano valide)'
        : editor.path
          ? `Sovrascrive ${editor.path}`
          : undefined}
    >
      <FileDown class="size-4" /> Salva
    </button>
    {#if choosesWhere}
      <button
        type="button"
        class={button}
        disabled={editor.busy}
        onclick={() => editor.save(true)}
        title="Salva con nome"
      >
        <FileDown class="size-4" /> <span class="sr-only min-[1900px]:not-sr-only">Salva con nome</span>
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
