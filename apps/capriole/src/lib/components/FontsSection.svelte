<script lang="ts">
  // Beside the page, while its text is edited: the fonts the person gives (chosen or dropped), remembered
  // on the device. An edit that needs a letter an embedded subset lacks takes its glyph from the same font
  // here, instead of writing it in a similar font; after such an edit, which fonts would have done it.
  import Plus from '@lucide/svelte/icons/plus';
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import Type from '@lucide/svelte/icons/type';
  import { getEditor } from '$lib/editor.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { fontsSectionWords } from '$lib/i18n/fontsSection';
  import { formatBytes } from '$lib/files';
  import { missingFontsHint } from '$lib/textedit';

  const editor = getEditor();
  const t = $derived(tr(fontsSectionWords));
  let input: HTMLInputElement | undefined = $state();
  let dragging = $state(false);
  let adding = $state(false);

  const hint = $derived(missingFontsHint(editor.missingFonts));

  async function add(files: FileList | null | undefined) {
    if (!files || files.length === 0) return;
    adding = true;
    try {
      await editor.addFonts([...files]);
    } finally {
      adding = false;
    }
  }

  const button =
    'flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm hover:bg-muted disabled:pointer-events-none disabled:opacity-50';
</script>

{#if hint}
  <section
    class="flex flex-col gap-2 rounded-md bg-warning-background px-3 py-2 text-warning"
    data-testid="missing-fonts"
  >
    <p>{hint}</p>
    <div>
      <button
        type="button"
        class="rounded-md border border-current px-2.5 py-1 font-medium disabled:opacity-60"
        disabled={adding}
        onclick={() => input?.click()}>{t.add}</button
      >
    </div>
  </section>
{/if}

<section
  class={['flex flex-col gap-2 rounded-md', dragging && 'ring-2 ring-emphasis']}
  aria-label={t.title}
  data-testid="fonts-section"
  ondragover={(e) => {
    if (!e.dataTransfer?.types.includes('Files')) return;
    e.preventDefault();
    dragging = true;
  }}
  ondragleave={() => (dragging = false)}
  ondrop={(e) => {
    e.preventDefault();
    dragging = false;
    add(e.dataTransfer?.files);
  }}
>
  <h2 class="flex items-center gap-1.5 font-semibold"><Type class="size-4" /> {t.title}</h2>
  <p class="text-xs text-muted-foreground">
    {t.explain}
  </p>
  {#if editor.fonts.length > 0}
    <ul class="flex flex-col gap-1" data-testid="font-list">
      {#each editor.fonts as font (font.id)}
        <li class="flex items-start gap-2 rounded-md px-2 py-1 hover:bg-muted">
          <div class="flex min-w-0 flex-1 flex-col">
            <span class="truncate" title={font.name}>{font.name}</span>
            {#each font.faces as face (face.postscriptName)}
              <span class={['truncate text-xs', face.embeddable ? 'text-muted-foreground' : 'text-warning']}
                >{face.family ? `${face.family} ${face.style}` : face.postscriptName}{face.embeddable
                  ? ''
                  : t.notEmbeddable}</span
              >
            {/each}
            <span class="text-xs text-muted-foreground">{formatBytes(font.data.byteLength)}</span>
          </div>
          <button
            type="button"
            class="rounded-md p-1 hover:bg-background"
            aria-label={t.remove(font.name)}
            onclick={() => editor.removeFont(font.id)}><Trash2 class="size-4" /></button
          >
        </li>
      {/each}
    </ul>
  {/if}
  {#if editor.fontError}
    <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{editor.fontError}</p>
  {/if}
  <div>
    <button type="button" class={button} disabled={adding} onclick={() => input?.click()}>
      <Plus class="size-4" />
      {t.add}
    </button>
  </div>
  <input
    bind:this={input}
    type="file"
    accept=".ttf,.ttc,.otf,font/ttf,font/otf,font/collection"
    multiple
    hidden
    data-testid="font-input"
    onchange={async (e) => {
      const target = e.currentTarget;
      await add(target.files);
      target.value = '';
    }}
  />
</section>
