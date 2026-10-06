<script lang="ts">
  // «Informazioni e licenze»: Capriole's licence (0BSD), and the notices of what it is built from — the
  // engine's dependencies, the JavaScript packages in the app, the resource pack, and on the desktop the
  // Rust crates of its shell. Read from licenses.json, which the build writes
  // (packages/engine-client/scripts/licenses.mjs).
  import Info from '@lucide/svelte/icons/info';
  import { base } from '$app/paths';
  import { getEditor } from '$lib/editor.svelte';
  import { isDesktop } from '$lib/platform';

  interface Notice {
    name: string;
    version: string;
    license: string;
    use: string;
    note: string | null;
    text: string;
  }
  interface Licenses {
    capriole: string;
    engine: Notice[];
    npm: { name: string; version: string; license: string; text: string }[];
    resources: { name: string; text: string }[];
    crates: { license: string; crates: string[]; text: string; source?: string[] }[];
  }

  const editor = getEditor();
  let data = $state.raw<Licenses | null>(null);
  let failure = $state<string | null>(null);

  async function load() {
    if (data) return;
    try {
      const response = await fetch(`${base}/licenses.json`);
      if (!response.ok) throw new Error(`${response.status}`);
      data = (await response.json()) as Licenses;
    } catch {
      failure = 'L’elenco delle licenze c’è solo nella versione compilata dell’app (npm run build).';
    }
  }

  // A modal dialog while it is open (showModal: focus trapped, Escape closes).
  function modal(dialog: HTMLDialogElement) {
    if (editor.licensesOpen && !dialog.open) {
      dialog.showModal();
      load();
    }
    if (!editor.licensesOpen && dialog.open) dialog.close();
  }
</script>

{#snippet text(body: string)}
  <pre class="mt-2 max-h-64 overflow-auto rounded-md bg-muted p-2 text-xs whitespace-pre-wrap">{body}</pre>
{/snippet}

<dialog
  {@attach modal}
  class="m-auto w-full max-w-2xl rounded-xl border bg-card p-6 text-foreground shadow-lg backdrop:bg-black/40"
  aria-labelledby="licenses-title"
  data-testid="licenses-dialog"
  oncancel={(e) => {
    e.preventDefault();
    editor.licensesOpen = false;
  }}
>
  <div class="flex max-h-[80dvh] flex-col gap-4 text-sm">
    <h2 id="licenses-title" class="flex items-center gap-2 text-base font-semibold">
      <Info class="size-4" /> Informazioni e licenze
    </h2>
    <div class="flex min-h-0 flex-col gap-4 overflow-y-auto pe-1">
      <p>
        Capriole {editor.engineVersion ? `(motore ${editor.engineVersion})` : ''} è software libero con licenza
        <strong>0BSD</strong>: puoi usarlo, copiarlo, modificarlo e distribuirlo per qualunque scopo. I tuoi file non
        lasciano il dispositivo.
      </p>
      {#if failure}
        <p class="rounded-md bg-warning-background px-3 py-2 text-warning" role="status">{failure}</p>
      {:else if !data}
        <p class="text-muted-foreground">Lettura…</p>
      {:else}
        <details>
          <summary class="cursor-pointer font-medium">Licenza di Capriole (0BSD)</summary>
          {@render text(data.capriole)}
        </details>

        <section class="flex flex-col gap-1" data-testid="licenses-engine">
          <h3 class="font-medium">Nel motore</h3>
          {#each data.engine as e (e.name)}
            <details>
              <summary class="cursor-pointer">{e.name} {e.version} — {e.license}</summary>
              <p class="mt-1 text-muted-foreground">Usato per: {e.use}.</p>
              {#if e.note}<p class="mt-1">{e.note}</p>{/if}
              {#if e.text}{@render text(e.text)}{/if}
            </details>
          {/each}
        </section>

        <section class="flex flex-col gap-1" data-testid="licenses-npm">
          <h3 class="font-medium">Nell’app (pacchetti JavaScript)</h3>
          {#each data.npm as p (`${p.name}@${p.version}`)}
            <details>
              <summary class="cursor-pointer">{p.name} {p.version} — {p.license || 'vedi il testo'}</summary>
              {@render text(p.text || p.license)}
            </details>
          {/each}
        </section>

        {#if data.resources.length > 0}
          <section class="flex flex-col gap-1">
            <h3 class="font-medium">Risorse (font, CMap, profilo colore)</h3>
            {#each data.resources as r (r.name)}
              <details>
                <summary class="cursor-pointer">{r.name}</summary>
                {@render text(r.text)}
              </details>
            {/each}
          </section>
        {/if}

        {#if isDesktop && data.crates.length > 0}
          <section class="flex flex-col gap-1" data-testid="licenses-crates">
            <h3 class="font-medium">Nell’app desktop (librerie Rust)</h3>
            {#each data.crates as c, i (i)}
              <details>
                <summary class="cursor-pointer">{c.crates.join(', ')} — {c.license}</summary>
                {#if c.source}
                  <p class="mt-1">Sorgente: {c.source.join(' ')}</p>
                {/if}
                {@render text(c.text)}
              </details>
            {/each}
          </section>
        {/if}
      {/if}
    </div>
    <div class="flex justify-end">
      <button
        type="button"
        class="rounded-md border px-3 py-1.5 hover:bg-muted"
        onclick={() => (editor.licensesOpen = false)}>Chiudi</button
      >
    </div>
  </div>
</dialog>
