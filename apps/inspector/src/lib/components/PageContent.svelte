<script lang="ts">
  // The operations of a page's content stream, indented by q/Q, BT/ET and marked content, the operator
  // coloured by its category (ISO 32000-2 Table 50).
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import Value from './Value.svelte';
  import type { ContentOperation } from '$lib/engine/types';
  import { getInspector } from '$lib/inspector.svelte';
  import { formatBytes } from '$lib/pdf/text';

  let { index }: { index: number } = $props();

  const inspector = getInspector();
  const kShown = 5000;
  const request = $derived(inspector.engine.pageContent(index, kShown));

  const opens = new Set(['q', 'BT', 'BMC', 'BDC']);
  const closes = new Set(['Q', 'ET', 'EMC']);

  function nested(operations: ContentOperation[]): { operation: ContentOperation; depth: number }[] {
    let depth = 0;
    return operations.map((operation) => {
      if (closes.has(operation.op)) depth = Math.max(0, depth - 1);
      const row = { operation, depth };
      if (opens.has(operation.op)) depth++;
      return row;
    });
  }

  const categoryClass: Record<string, string> = {
    general_graphics_state: 'text-muted-foreground',
    special_graphics_state: 'text-muted-foreground font-semibold',
    path_construction: 'text-code-number',
    path_painting: 'text-code-number font-semibold',
    clipping: 'text-code-number font-semibold',
    text_object: 'text-code-string font-semibold',
    text_state: 'text-code-string',
    text_positioning: 'text-code-string',
    text_showing: 'text-code-string font-semibold',
    color: 'text-code-name',
    shading: 'text-code-ref font-semibold',
    inline_image: 'text-code-ref font-semibold',
    xobject: 'text-code-ref font-semibold',
    marked_content: 'text-warning',
    unknown: 'text-danger font-semibold',
  };
</script>

<section class="mt-6">
  {#await request}
    <p class="text-sm text-muted-foreground">Lettura del contenuto…</p>
  {:then content}
    <div class="mb-2 flex flex-wrap items-baseline gap-3">
      <h3 class="font-semibold">Contenuto della pagina {content.page + 1}</h3>
      <span class="text-sm text-muted-foreground">
        {content.total} operazioni · {formatBytes(content.size)}
      </span>
    </div>
    {#if content.recovered > 0}
      <p class="mb-2 flex items-center gap-2 rounded-md bg-warning-background px-3 py-2 text-sm text-warning">
        <TriangleAlert class="size-4" /> Errori di sintassi saltati: {content.recovered}
      </p>
    {/if}
    <ol class="max-h-[70vh] overflow-auto rounded-lg border bg-card py-2 font-mono text-xs leading-relaxed">
      {#each nested(content.operations) as { operation, depth } (operation.o)}
        <li class="flex gap-3 px-3 hover:bg-muted">
          <span class="w-16 shrink-0 text-end text-muted-foreground select-none">{operation.o}</span>
          <span class="inline-flex min-w-0 flex-wrap gap-x-[1ch]" style:padding-inline-start="{depth * 1.25}rem">
            {#each operation.operands as operand, i (i)}<Value value={operand} depth={2} />{/each}
            <span class={categoryClass[operation.category] ?? ''}>{operation.op}</span>
            {#if operation.image}
              <span class="text-muted-foreground">… ID {formatBytes(operation.image.size)} EI</span>
            {/if}
          </span>
        </li>
      {/each}
    </ol>
    {#if content.total > content.operations.length}
      <p class="mt-1 text-xs text-muted-foreground">
        Mostrate le prime {content.operations.length} operazioni su {content.total}.
      </p>
    {/if}
  {:catch error}
    <p class="rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">
      {error instanceof Error ? error.message : String(error)}
    </p>
  {/await}
</section>
