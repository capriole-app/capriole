<script lang="ts">
  // The app, once its code has loaded in the browser ([[lang=locale]]/+page.svelte): the dialogs, the
  // shortcuts and, with a file open, the pages and the page shown large. Without a file the first screen
  // is the page's landing, which asks for this editor through lib/editor-context.ts.
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import AnnotationToolbar from './AnnotationToolbar.svelte';
  import AttachmentsPanel from './AttachmentsPanel.svelte';
  import PageGrid from './PageGrid.svelte';
  import PageView from './PageView.svelte';
  import PasswordDialog from './PasswordDialog.svelte';
  import LicensesDialog from './LicensesDialog.svelte';
  import TermsDialog from './TermsDialog.svelte';
  import ReportDialog from './ReportDialog.svelte';
  import DownloadDialog from './DownloadDialog.svelte';
  import { isDesktop } from '$lib/platform';
  import { reportWords } from '$lib/i18n/report';
  import SettingsDialog from './SettingsDialog.svelte';
  import PdfaDialog from './PdfaDialog.svelte';
  import SignaturesPanel from './SignaturesPanel.svelte';
  import SignDialog from './SignDialog.svelte';
  import Toolbar from './Toolbar.svelte';
  import { setEditor, type Editor } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { pagesWords } from '$lib/i18n/pages';
  import { gridRange, pageRange, zoomIn, zoomOut } from '$lib/zoom';

  let { editor }: { editor: Editor } = $props();
  // Made once by the page, before this mounts: it does not change.
  // svelte-ignore state_referenced_locally
  setEditor(editor);
  const t = $derived(tr(pagesWords));
  const rt = $derived(tr(reportWords));

  /**
   * Keyboard shortcuts — not while typing. On the pages: Delete, Ctrl/Cmd+Z, Ctrl/Cmd+A. On a page shown
   * large: Delete removes the selected annotation, Escape lets it go, then goes back to the pages. On both:
   * Ctrl/Cmd + / − / 0 zoom.
   */
  function shortcut(e: KeyboardEvent) {
    const typing =
      e.target instanceof HTMLInputElement ||
      e.target instanceof HTMLTextAreaElement ||
      e.target instanceof HTMLSelectElement ||
      (e.target instanceof HTMLElement && e.target.isContentEditable);
    if (
      !editor.name ||
      editor.busy ||
      typing ||
      editor.typing ||
      editor.signRequest ||
      editor.pdfaDialog !== null ||
      editor.licensesOpen ||
      editor.termsOpen ||
      editor.reportOpen ||
      editor.downloadOpen ||
      editor.settingsOpen
    )
      return;
    const command = e.ctrlKey || e.metaKey;
    // Zoom: Ctrl/Cmd and + or −; 0 fits the page (on the grid: the usual size). Not the browser's zoom.
    if (command && ['+', '=', '-', '0'].includes(e.key)) {
      e.preventDefault();
      if (editor.viewing !== null) {
        const now = editor.pageZoomShown;
        editor.pageZoom = e.key === '0' ? 'fit' : e.key === '-' ? zoomOut(now, pageRange) : zoomIn(now, pageRange);
      } else {
        const now = editor.gridZoom;
        editor.zoomGrid(e.key === '0' ? 100 : e.key === '-' ? zoomOut(now, gridRange) : zoomIn(now, gridRange));
      }
      return;
    }
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
<LicensesDialog />
<TermsDialog />
<ReportDialog />
{#if !isDesktop}<DownloadDialog />{/if}
<SettingsDialog />

{#if editor.name}
  <!-- On a phone, the bars' commands along the bottom (ui.ts bottomBar): their height left free. -->
  <div class="flex h-dvh flex-col max-md:pb-14">
    {#if editor.viewing !== null}
      <AnnotationToolbar />
    {:else}
      <Toolbar />
    {/if}
    {#if editor.error}
      <p class="flex items-center gap-3 bg-danger-background px-4 py-2 text-sm text-danger" role="alert">
        <span class="flex-1">{editor.error}</span>
        {#if !isDesktop}
          <button type="button" class="shrink-0 underline underline-offset-2" onclick={() => (editor.reportOpen = true)}
            >{rt.reportThis}</button
          >
        {/if}
      </p>
    {/if}
    {#if editor.repairs.length > 0}
      <details class="bg-warning-background px-4 py-2 text-sm text-warning">
        <summary class="flex cursor-pointer items-center gap-2">
          <TriangleAlert class="size-4" />
          {t.repaired(editor.repairs.length)}
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
        <main class="min-h-0 min-w-0 flex-1 overflow-y-auto bg-background">
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
