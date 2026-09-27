<script lang="ts">
  import { getInspector } from '$lib/inspector.svelte';

  const inspector = getInspector();
  const pages = $derived(inspector.summary?.pages ?? []);
</script>

{#if inspector.summary?.pages === null}
  <p class="rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">
    Albero delle pagine illeggibile: {inspector.summary?.pagesError}
  </p>
{:else}
  <ul class="-mx-1 overflow-y-auto">
    {#each pages as [number, generation], index (number)}
      <li>
        <button
          type="button"
          class={[
            'flex w-full items-center gap-2 rounded px-2 py-1 text-start text-sm hover:bg-muted',
            inspector.current?.number === number && 'bg-accent',
          ]}
          onclick={() => inspector.show(number)}
        >
          <span class="flex-1">Pagina {index + 1}</span>
          <span class="font-mono text-xs text-code-ref">{number} {generation} R</span>
        </button>
      </li>
    {/each}
  </ul>
{/if}
