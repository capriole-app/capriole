<script lang="ts">
  // The colour of the next annotation and the size of the next free text (or of the free text selected),
  // in a menu under one button that shows the colour: the page's bar stays on one line at 1440 px.
  import ChevronDown from '@lucide/svelte/icons/chevron-down';
  import { fontSizes } from '$lib/settings.svelte';
  import { getEditor, type Rgb } from '$lib/editor.svelte';
  import { colors, css, sameColor } from '$lib/colors';
  import { tr } from '$lib/i18n/index.svelte';
  import { annotationToolbarWords } from '$lib/i18n/annotationToolbar';
  import { barButton, menuLabel, pressed } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(annotationToolbarWords));
  let open = $state(false);
  const selectedText = $derived(editor.annotation?.subtype === 'FreeText' ? editor.annotation : null);
</script>

<svelte:window onclick={() => (open = false)} onkeydown={(e) => e.key === 'Escape' && (open = false)} />

<div class="relative">
  <button
    type="button"
    class={[barButton, 'gap-1', open && pressed]}
    aria-label={t.style}
    title={t.style}
    aria-haspopup="dialog"
    aria-expanded={open}
    data-testid="style-menu"
    onclick={(e) => {
      e.stopPropagation();
      open = !open;
    }}
  >
    <!-- A hairline around it: black stays visible on the dark bar. -->
    <span class="size-5 rounded-full ring-1 ring-foreground/25" style:background-color={css(editor.color)}></span>
    <ChevronDown class="size-3.5 text-muted-foreground" />
  </button>
  {#if open}
    <!-- A click inside leaves it open (the window's click closes it); choosing a colour or a size closes it. -->
    <!-- svelte-ignore a11y_click_events_have_key_events -->
    <div
      role="dialog"
      tabindex="-1"
      aria-label={t.style}
      class="absolute start-0 z-20 mt-1 w-max rounded-lg border bg-card p-2 shadow-lg"
      onclick={(e) => e.stopPropagation()}
    >
      <p class={menuLabel}>{t.color}</p>
      <div class="flex items-center gap-1 px-2 pb-2" role="radiogroup" aria-label={t.color}>
        {#each colors as { name, rgb } (name)}
          <button
            type="button"
            role="radio"
            aria-checked={sameColor(editor.color, rgb)}
            aria-label={name}
            title={name}
            class={[
              'size-7 rounded-full border-2 ring-1 ring-foreground/25',
              sameColor(editor.color, rgb) ? 'border-foreground' : 'border-transparent',
            ]}
            style:background-color={css(rgb)}
            onclick={() => {
              editor.color = [...rgb] as Rgb;
              open = false;
            }}
          ></button>
        {/each}
      </div>
      <label class="block px-2 pb-1">
        <span class="block pt-1 pb-1 text-xs font-medium text-muted-foreground">{t.textSize}</span>
        <select
          class="h-9 rounded-md border bg-card px-2 text-sm"
          value={selectedText?.fontSize ? Math.round(selectedText.fontSize) : editor.fontSize}
          onchange={(e) => {
            const size = Number(e.currentTarget.value);
            editor.fontSize = size;
            if (selectedText?.ref) editor.changeAnnotation(selectedText.ref, { fontSize: size });
            open = false;
          }}
        >
          {#each fontSizes as size (size)}
            <option value={size}>{size} pt</option>
          {/each}
        </select>
      </label>
    </div>
  {/if}
</div>
