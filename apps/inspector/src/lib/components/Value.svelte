<script lang="ts">
  // A PDF value, recursively: dictionaries as key/value rows, references as links to their object.
  import Value from './Value.svelte';
  import type { PdfValue } from '@capriole/engine-client';
  import { getInspector } from '$lib/inspector.svelte';
  import { hexOf, latin1Bytes, nameOf, textOf } from '$lib/pdf/text';

  let { value, depth = 0 }: { value: PdfValue; depth?: number } = $props();

  const inspector = getInspector();

  // Short arrays of plain values read best on one line ([0 0 612 792]).
  const inline = $derived(
    value.t === 'array' && value.v.length <= 12 && value.v.every((v) => v.t !== 'array' && v.t !== 'dict'),
  );
  const text = $derived(value.t === 'string' ? textOf(latin1Bytes(value.v)) : null);
</script>

{#if value.t === 'null'}
  <span class="text-muted-foreground">null</span>
{:else if value.t === 'bool'}
  <span class="text-code-number">{value.v}</span>
{:else if value.t === 'int' || value.t === 'real'}
  <span class="text-code-number">{value.v}</span>
{:else if value.t === 'name'}
  <span class="text-code-name">/{nameOf(value.v)}</span>
{:else if value.t === 'string'}
  {#if text !== null}
    <span class="break-all text-code-string" title="stringa {value.form === 'hex' ? 'esadecimale' : 'letterale'}"
      >({text})</span
    >
  {:else}
    <span class="break-all text-code-string" title="stringa binaria">&lt;{hexOf(latin1Bytes(value.v), 512)}&gt;</span>
  {/if}
{:else if value.t === 'ref'}
  <button type="button" class="text-code-ref hover:underline" onclick={() => inspector.show(value.n)}
    >{value.n} {value.g} R</button
  >
{:else if value.t === 'array'}
  {#if inline}
    <!-- Array elements have no identity but their position, and the list never reorders: the index is the key. -->
    <span
      >[<span class="inline-flex flex-wrap gap-x-[1ch]"
        >{#each value.v as element, i (i)}<Value value={element} depth={depth + 1} />{/each}</span
      >]</span
    >
  {:else}
    <details open={depth < 2} class="ms-1">
      <summary class="cursor-pointer text-muted-foreground select-none">array, {value.v.length} elementi</summary>
      <ol class="ms-4 border-s ps-3" start="0">
        {#each value.v as element, i (i)}
          <li class="py-0.5">
            <span class="me-2 text-xs text-muted-foreground">{i}</span><Value value={element} depth={depth + 1} />
          </li>
        {/each}
      </ol>
    </details>
  {/if}
{:else if value.t === 'dict' || value.t === 'stream'}
  {@const entries = value.t === 'dict' ? value.v : value.dict}
  {#if entries.length === 0}
    <span class="text-muted-foreground">&lt;&lt; &gt;&gt;</span>
  {:else}
    <details open={depth < 2}>
      <summary class="cursor-pointer text-muted-foreground select-none">
        {value.t === 'stream' ? 'stream' : 'dizionario'}, {entries.length} voci
      </summary>
      <table class="ms-4 border-s">
        <tbody>
          {#each entries as [key, entry] (key)}
            <tr class="align-top">
              <td class="py-0.5 ps-3 pe-4 whitespace-nowrap text-code-name">/{nameOf(key)}</td>
              <td class="py-0.5"><Value value={entry} depth={depth + 1} /></td>
            </tr>
          {/each}
        </tbody>
      </table>
    </details>
  {/if}
{/if}
