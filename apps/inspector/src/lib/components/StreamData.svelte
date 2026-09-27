<script lang="ts">
  // The data of a stream: decoded (filters undone) or as stored, as text or hex dump.
  import Download from '@lucide/svelte/icons/download';
  import { getInspector, download } from '$lib/inspector.svelte';
  import { formatBytes, hexDump, looksLikeText } from '$lib/pdf/text';

  let { number }: { number: number } = $props();

  const inspector = getInspector();
  const kTextLimit = 256 * 1024;
  const kHexLimit = 16 * 1024;

  let decoded = $state(true);
  let view = $state<'auto' | 'text' | 'hex'>('auto');

  // The data comes from the worker; {#await} shows the latest request only.
  const request = $derived(inspector.engine.stream(number, decoded).then((buffer) => new Uint8Array(buffer)));

  function presentation(data: Uint8Array, mode: typeof view) {
    const asText = mode === 'text' || (mode === 'auto' && looksLikeText(data));
    const shown = asText ? new TextDecoder('latin1').decode(data.subarray(0, kTextLimit)) : hexDump(data, kHexLimit);
    return { shown, truncated: data.length > (asText ? kTextLimit : kHexLimit) };
  }
</script>

<section class="mt-6">
  <div class="mb-2 flex flex-wrap items-center gap-3">
    <h3 class="font-semibold">Dati dello stream</h3>
    <div class="flex rounded-md border text-sm" role="group" aria-label="Forma dei dati">
      <button type="button" class={['px-2 py-1', decoded && 'bg-accent']} onclick={() => (decoded = true)}
        >decodificati</button
      >
      <button type="button" class={['border-s px-2 py-1', !decoded && 'bg-accent']} onclick={() => (decoded = false)}
        >come nel file</button
      >
    </div>
    <div class="flex rounded-md border text-sm" role="group" aria-label="Vista">
      {#each [['auto', 'auto'], ['text', 'testo'], ['hex', 'esadecimale']] as const as [key, label] (key)}
        <button
          type="button"
          class={['px-2 py-1 not-first:border-s', view === key && 'bg-accent']}
          onclick={() => (view = key)}>{label}</button
        >
      {/each}
    </div>
    {#await request then data}
      <span class="text-sm text-muted-foreground">{formatBytes(data.length)}</span>
      <button
        type="button"
        class="ms-auto flex items-center gap-1 rounded-md border px-2 py-1 text-sm hover:bg-muted"
        onclick={() => download(new Blob([data.slice().buffer]), `oggetto-${number}${decoded ? '' : '-grezzo'}.bin`)}
      >
        <Download class="size-4" /> Scarica
      </button>
    {/await}
  </div>
  {#await request}
    <p class="text-sm text-muted-foreground">Lettura…</p>
  {:then data}
    {@const { shown, truncated } = presentation(data, view)}
    <pre
      class="max-h-[60vh] overflow-auto rounded-md border bg-muted p-3 font-mono text-xs leading-relaxed">{shown}</pre>
    {#if truncated}
      <p class="mt-1 text-xs text-muted-foreground">Mostrata solo la prima parte: scarica i dati per vederli tutti.</p>
    {/if}
  {:catch error}
    <p class="rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">
      {error instanceof Error ? error.message : String(error)}
    </p>
  {/await}
</section>
