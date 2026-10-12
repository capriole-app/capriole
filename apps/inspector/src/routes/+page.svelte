<script lang="ts">
  import AppHeader from '$lib/components/AppHeader.svelte';
  import DropZone from '$lib/components/DropZone.svelte';
  import ObjectList from '$lib/components/ObjectList.svelte';
  import ObjectView from '$lib/components/ObjectView.svelte';
  import Overview from '$lib/components/Overview.svelte';
  import PageList from '$lib/components/PageList.svelte';
  import PasswordDialog from '$lib/components/PasswordDialog.svelte';
  import { Inspector, setInspector, type Tab } from '$lib/inspector.svelte';

  const inspector = setInspector(new Inspector());

  const tabs: { key: Tab; label: string }[] = [
    { key: 'overview', label: 'Panoramica' },
    { key: 'pages', label: 'Pagine' },
    { key: 'objects', label: 'Oggetti' },
  ];
</script>

<PasswordDialog />

{#if !inspector.summary}
  <DropZone />
{:else}
  <div class="flex h-dvh flex-col">
    <AppHeader />
    {#if inspector.error}
      <p class="bg-danger-background px-4 py-2 text-sm text-danger" role="alert">{inspector.error}</p>
    {/if}
    <div class="flex min-h-0 flex-1">
      <nav class="flex w-64 shrink-0 flex-col border-e bg-card p-3" aria-label="Navigazione del documento">
        <div class="mb-3 flex rounded-md border text-sm" role="tablist">
          {#each tabs as tab (tab.key)}
            <button
              type="button"
              role="tab"
              aria-selected={inspector.tab === tab.key}
              class={['flex-1 px-2 py-1.5 not-first:border-s', inspector.tab === tab.key && 'bg-accent font-medium']}
              onclick={() => (inspector.tab = tab.key)}>{tab.label}</button
            >
          {/each}
        </div>
        <div class="min-h-0 flex-1 overflow-y-auto">
          {#if inspector.tab === 'pages'}
            <PageList />
          {:else if inspector.tab === 'objects'}
            <ObjectList />
          {:else}
            <p class="text-sm text-muted-foreground">
              Il riepilogo del documento è a destra. Da qui: le pagine, o tutti gli oggetti della tabella xref.
            </p>
          {/if}
        </div>
      </nav>
      <main class="min-w-0 flex-1 overflow-y-auto p-6">
        {#if inspector.tab === 'overview'}
          <Overview />
        {:else}
          <ObjectView />
        {/if}
      </main>
    </div>
  </div>
{/if}
