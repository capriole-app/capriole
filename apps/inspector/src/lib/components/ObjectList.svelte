<script lang="ts">
  import Search from '@lucide/svelte/icons/search';
  import { getInspector } from '$lib/inspector.svelte';

  const inspector = getInspector();
  const kShown = 400;

  let filter = $state('');
  let showFree = $state(false);

  const objects = $derived(inspector.summary?.xref.objects ?? []);
  const matching = $derived.by(() => {
    const wanted = filter.trim();
    return objects.filter(
      ([number, kind]) => (showFree || kind !== 'f') && (wanted === '' || String(number).startsWith(wanted)),
    );
  });
  const kindLabel = { n: 'nel file', c: 'in object stream', f: 'libero' } as const;
</script>

<div class="flex h-full flex-col">
  <label class="relative mb-2 block">
    <Search class="absolute start-2 top-2.5 size-4 text-muted-foreground" />
    <input
      class="w-full rounded-md border bg-background py-1.5 ps-8 pe-2 text-base sm:text-sm"
      placeholder="Numero oggetto"
      inputmode="numeric"
      bind:value={filter}
    />
  </label>
  <label class="mb-2 flex items-center gap-2 text-xs text-muted-foreground">
    <input type="checkbox" bind:checked={showFree} /> mostra anche le voci libere
  </label>
  <p class="mb-1 text-xs text-muted-foreground">{matching.length} oggetti</p>
  <ul class="-mx-1 flex-1 overflow-y-auto">
    {#each matching.slice(0, kShown) as [number, kind, detail] (number)}
      <li>
        <button
          type="button"
          class={[
            'flex w-full items-center gap-2 rounded px-2 py-1 text-start text-sm hover:bg-muted',
            inspector.current?.number === number && 'bg-accent',
          ]}
          onclick={() => inspector.show(number)}
        >
          <span class="w-14 font-mono">{number}</span>
          <span class="flex-1 truncate text-xs text-muted-foreground">
            {kindLabel[kind]}{kind === 'c' ? ` ${detail}` : ''}
          </span>
        </button>
      </li>
    {/each}
  </ul>
  {#if matching.length > kShown}
    <p class="mt-1 text-xs text-muted-foreground">Solo i primi {kShown}: filtra per numero per trovare gli altri.</p>
  {/if}
</div>
