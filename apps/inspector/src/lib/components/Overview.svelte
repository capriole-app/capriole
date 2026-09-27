<script lang="ts">
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import Lock from '@lucide/svelte/icons/lock';
  import Value from './Value.svelte';
  import { getInspector } from '$lib/inspector.svelte';
  import { formatBytes } from '$lib/pdf/text';

  const inspector = getInspector();
  const summary = $derived(inspector.summary);

  const formLabel = {
    table: 'tabella',
    stream: 'xref stream',
    hybrid: 'ibrida',
    reconstructed: 'ricostruita',
  } as const;

  // /P bits (§7.6.4.2, Table 22) that matter to a user.
  const permissionBits = [
    [3, 'stampa'],
    [4, 'modifica'],
    [5, 'copia del testo'],
    [6, 'annotazioni'],
    [9, 'moduli'],
    [11, 'assemblaggio'],
    [12, 'stampa ad alta qualità'],
  ] as const;
</script>

{#if summary}
  <div class="grid gap-4">
    {#if summary.repairs.length > 0}
      <section class="rounded-lg border bg-warning-background p-4 text-warning">
        <h3 class="mb-2 flex items-center gap-2 font-semibold">
          <TriangleAlert class="size-4" /> Riparazioni applicate: {summary.repairs.length}
        </h3>
        <p class="mb-2 text-sm">
          Il file non è conforme: il motore lo legge riparandolo in memoria. Un salvataggio completo scrive la versione
          riparata; quello incrementale non è possibile.
        </p>
        <ul class="max-h-64 space-y-1 overflow-y-auto font-mono text-xs">
          {#each summary.repairs as repair, i (i)}
            <li><span class="font-semibold">{repair.kind}</span> — {repair.description}</li>
          {/each}
        </ul>
      </section>
    {/if}

    <section class="grid gap-x-8 gap-y-2 rounded-lg border bg-card p-4 text-sm sm:grid-cols-2">
      <div>
        <span class="text-muted-foreground">Versione PDF</span> <span class="ms-2 font-medium">{summary.version}</span>
      </div>
      <div>
        <span class="text-muted-foreground">Dimensione</span>
        <span class="ms-2 font-medium">{formatBytes(summary.size)}</span>
      </div>
      <div>
        <span class="text-muted-foreground">Pagine</span>
        <span class="ms-2 font-medium">{summary.pages?.length ?? 'illeggibili'}</span>
      </div>
      <div>
        <span class="text-muted-foreground">Oggetti</span>
        <span class="ms-2 font-medium">{summary.xref.objects.filter((o) => o[1] !== 'f').length}</span>
      </div>
    </section>

    {#if summary.encryption}
      {@const enc = summary.encryption}
      <section class="rounded-lg border bg-card p-4 text-sm">
        <h3 class="mb-2 flex items-center gap-2 font-semibold"><Lock class="size-4" /> Cifratura</h3>
        <p>
          Revisione {enc.revision}: stringhe {enc.strings}, stream {enc.streams}{enc.encryptMetadata
            ? ''
            : ', metadata in chiaro'}. Aperto come {enc.owner ? 'proprietario' : 'utente'}.
        </p>
        <p class="mt-1 text-muted-foreground">
          Permessi dell'utente:
          {#each permissionBits as [bit, label], i (bit)}
            {i > 0 ? ', ' : ''}<span class={(enc.permissions >> (bit - 1)) & 1 ? '' : 'line-through'}>{label}</span>
          {/each}
        </p>
      </section>
    {/if}

    <section class="rounded-lg border bg-card p-4 text-sm">
      <h3 class="mb-2 font-semibold">Tabelle dei riferimenti incrociati</h3>
      <p class="mb-2 text-muted-foreground">startxref {summary.xref.startxref}; la più recente per prima.</p>
      <table class="w-full text-start">
        <thead class="text-xs text-muted-foreground">
          <tr
            ><th class="py-1 text-start font-normal">Offset</th><th class="text-start font-normal">Forma</th><th
              class="text-start font-normal">Voci</th
            ></tr
          >
        </thead>
        <tbody class="font-mono">
          {#each summary.xref.sections as section, i (i)}
            <tr class="border-t"
              ><td class="py-1">{section.offset}</td><td>{formLabel[section.form]}</td><td>{section.entries}</td></tr
            >
          {/each}
        </tbody>
      </table>
    </section>

    <section class="overflow-x-auto rounded-lg border bg-card p-4">
      <h3 class="mb-2 text-sm font-semibold">Trailer</h3>
      <div class="font-mono text-sm"><Value value={summary.trailer} /></div>
    </section>
  </div>
{/if}
