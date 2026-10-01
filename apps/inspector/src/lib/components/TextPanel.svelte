<script lang="ts">
  // The page's text as the engine extracts it (phase 7): plain text in reading order, and where the
  // selected word comes from — the text operator in the content stream, the byte of its code, the font.
  import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
  import type { PageText } from '$lib/engine/types';
  import { getInspector } from '$lib/inspector.svelte';

  let { text, selected }: { text: PageText; selected: number | null } = $props();

  const inspector = getInspector();

  const word = $derived.by(() => {
    if (selected === null) return null;
    const range = text.words[selected];
    if (!range) return null;
    const chars = text.chars.slice(range[4], range[4] + range[5]);
    return { text: chars.map((c) => c[0]).join(''), first: chars[0]!, invisible: chars.some((c) => c[7] === 1) };
  });
</script>

<div class="mt-3 rounded-lg border bg-card p-3 text-sm">
  <p class="text-muted-foreground">
    {text.blocks.length} blocchi · {text.lines.length} righe · {text.words.length} parole · {text.chars.length} caratteri.
    Seleziona o fai clic su una parola dell'anteprima per vedere da dove viene.
  </p>
  {#if word}
    {@const [, , , , , size, angle, , stream, offset, operation, byte, font] = word.first}
    <dl class="mt-2 grid grid-cols-[max-content_1fr] gap-x-4 gap-y-1" data-testid="word-provenance">
      <dt class="text-muted-foreground">Parola</dt>
      <dd class="font-mono">«{word.text}»{word.invisible ? ' (invisibile)' : ''}</dd>
      <dt class="text-muted-foreground">Operatore</dt>
      <dd>
        offset {offset}, operazione n. {operation + 1} —
        {#if stream}
          stream
          <button type="button" class="font-mono text-code-ref underline" onclick={() => inspector.show(stream[0])}
            >{stream[0]} {stream[1]} R</button
          >
        {:else}
          contenuto della pagina (evidenziata qui sotto)
        {/if}
      </dd>
      <dt class="text-muted-foreground">Codice</dt>
      <dd>dal byte {byte} delle stringhe dell'operatore</dd>
      <dt class="text-muted-foreground">Font</dt>
      <dd>
        {#if font}
          <button type="button" class="font-mono text-code-ref underline" onclick={() => inspector.show(font[0])}
            >{font[0]} {font[1]} R</button
          >
        {:else}
          senza riferimento (diretto o sostituto)
        {/if}
        · corpo {size} pt{angle ? ` · ruotato ${angle}°` : ''}
      </dd>
    </dl>
  {/if}
  <details class="mt-2">
    <summary class="cursor-pointer">Testo in ordine di lettura</summary>
    <pre
      class="mt-2 max-h-[50vh] overflow-auto rounded-md bg-muted p-2 text-xs whitespace-pre-wrap"
      data-testid="page-text">{text.text}</pre>
  </details>
  {#if text.warnings.length > 0}
    <p class="mt-2 flex items-center gap-2 text-xs text-warning">
      <TriangleAlert class="size-4" /> Avvisi dell'interprete: {text.warnings.length} (vedi l'anteprima)
    </p>
  {/if}
</div>
