<script lang="ts">
  import ArrowLeft from '@lucide/svelte/icons/arrow-left';
  import StreamData from './StreamData.svelte';
  import Value from './Value.svelte';
  import { getInspector } from '$lib/inspector.svelte';

  const inspector = getInspector();
  const current = $derived(inspector.current);
</script>

{#if current}
  <article>
    <header class="mb-4 flex items-center gap-3">
      <button
        type="button"
        class="flex size-8 items-center justify-center rounded-md border hover:bg-muted disabled:opacity-40"
        aria-label="Indietro"
        disabled={inspector.history.length === 0}
        onclick={() => inspector.back()}
      >
        <ArrowLeft class="size-4" />
      </button>
      <h2 class="text-lg font-semibold">Oggetto {current.number} {current.generation}</h2>
      <span class="rounded bg-muted px-2 py-0.5 text-xs text-muted-foreground">{current.object.t}</span>
      {#if current.state !== 'unmodified'}
        <span class="rounded bg-accent px-2 py-0.5 text-xs">{current.state}</span>
      {/if}
    </header>
    <div class="overflow-x-auto rounded-lg border bg-card p-4 font-mono text-sm">
      <Value value={current.object} />
      {#if current.object.t === 'stream'}
        <p class="mt-2 text-xs text-muted-foreground">
          {current.object.length} byte nel file{current.object.offset >= 0
            ? `, dall'offset ${current.object.offset}`
            : ''}
        </p>
      {/if}
    </div>
    {#if current.object.t === 'stream'}
      <StreamData number={current.number} />
    {/if}
  </article>
{:else}
  <p class="text-muted-foreground">Scegli un oggetto dall'elenco, o segui un riferimento.</p>
{/if}
