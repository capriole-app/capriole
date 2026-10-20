<script lang="ts">
  // The settings: what the app remembers on this device (settings.svelte.ts), each change kept at once —
  // the language and theme, the zoom, what annotations are made with, a digital signature's defaults, and
  // the graphic signatures (graphic-signatures.svelte.ts).
  import ImagePlus from '@lucide/svelte/icons/image-plus';
  import Settings from '@lucide/svelte/icons/settings';
  import Trash2 from '@lucide/svelte/icons/trash-2';
  import { setMode, userPrefersMode } from 'mode-watcher';
  import { tick } from 'svelte';
  import { getEditor } from '$lib/editor.svelte';
  import { colors, css, sameColor } from '$lib/colors';
  import { isDesktop } from '$lib/platform';
  import { fontSizes, settings, type Rgb } from '$lib/settings.svelte';
  import { graphicSignatures, maxSignatures, showPicture, SignatureRefused } from '$lib/graphic-signatures.svelte';
  import { gridRange } from '$lib/zoom';
  import { i18n, languageNames, locales, tr, type Locale } from '$lib/i18n/index.svelte';
  import { settingsWords } from '$lib/i18n/settings';
  import { menusWords } from '$lib/i18n/menus';
  import { graphicSignatureWords } from '$lib/i18n/graphicSignatures';
  import { dialog, dialogTitle, field, secondaryButton } from '$lib/ui';

  const editor = getEditor();
  const t = $derived(tr(settingsWords));
  const m = $derived(tr(menusWords));
  const g = $derived(tr(graphicSignatureWords));
  const values = $derived(settings.values);

  let whiteToTransparent = $state(true);
  let problem = $state<string | null>(null);
  let adding = $state(false);

  // A modal dialog while it is open (showModal: focus trapped, Escape closes); scrolled to the graphic
  // signatures when opened for them.
  function modal(dialog: HTMLDialogElement) {
    if (editor.settingsOpen && !dialog.open) {
      problem = null;
      dialog.showModal();
      if (editor.settingsOpen === 'signatures')
        tick().then(() => dialog.querySelector('#settings-signatures')?.scrollIntoView());
    }
    if (!editor.settingsOpen && dialog.open) dialog.close();
  }

  async function add(file: File) {
    problem = null;
    adding = true;
    try {
      if (!(await graphicSignatures.add(file, whiteToTransparent))) problem = g.notKept;
    } catch (error) {
      problem = error instanceof SignatureRefused ? error.message : g.unreadable;
    } finally {
      adding = false;
    }
  }

  function setColor(which: 'highlightColor' | 'drawColor', rgb: Rgb) {
    settings.update({ [which]: rgb });
    // The session's colour too: what is drawn next.
    if (which === 'highlightColor') editor.highlightColor = [...rgb];
    else editor.drawColor = [...rgb];
  }

  const section = 'flex flex-col gap-3 border-t pt-4';
  const heading = 'text-sm font-semibold';
</script>

{#snippet swatches(which: 'highlightColor' | 'drawColor', label: string, hint: string)}
  <div class="flex flex-col gap-1">
    <span class="font-medium" id="settings-{which}">{label}</span>
    <div class="flex items-center gap-1.5" role="radiogroup" aria-labelledby="settings-{which}">
      {#each colors as { name, rgb } (name)}
        <button
          type="button"
          role="radio"
          aria-checked={sameColor(values[which], rgb)}
          aria-label={name}
          title={name}
          class={[
            'size-7 rounded-full border-2',
            sameColor(values[which], rgb) ? 'border-foreground' : 'border-transparent',
          ]}
          style:background-color={css(rgb)}
          onclick={() => setColor(which, [...rgb])}
        ></button>
      {/each}
    </div>
    <span class="text-xs text-muted-foreground">{hint}</span>
  </div>
{/snippet}

<dialog
  {@attach modal}
  class={[dialog, 'max-w-xl']}
  aria-labelledby="settings-title"
  data-testid="settings-dialog"
  oncancel={(e) => {
    e.preventDefault();
    editor.settingsOpen = false;
  }}
>
  <!-- Made only while open: its fields are not on the page otherwise. -->
  {#if editor.settingsOpen}
    <div class="flex max-h-[85dvh] flex-col gap-4 text-sm">
      <h2 id="settings-title" class={dialogTitle}>
        <Settings class="size-4" />
        {t.title}
      </h2>
      <p class="text-muted-foreground">{t.saved}</p>

      <div class="flex min-h-0 flex-col gap-4 overflow-y-auto pe-1">
        <section class="flex flex-col gap-3">
          <h3 class={heading}>{t.general}</h3>
          <div class="grid grid-cols-2 gap-3">
            <label class="flex flex-col gap-1">
              <span class="font-medium">{t.language}</span>
              <select
                class={field}
                value={i18n.locale}
                onchange={(e) => (i18n.locale = e.currentTarget.value as Locale)}
              >
                {#each locales as locale (locale)}
                  <option value={locale}>{languageNames[locale]}</option>
                {/each}
              </select>
            </label>
            <label class="flex flex-col gap-1">
              <span class="font-medium">{t.theme}</span>
              <select
                class={field}
                value={userPrefersMode.current}
                onchange={(e) => setMode(e.currentTarget.value as 'light' | 'dark' | 'system')}
              >
                <option value="system">{m.system}</option>
                <option value="light">{m.light}</option>
                <option value="dark">{m.dark}</option>
              </select>
            </label>
          </div>
        </section>

        <section class={section}>
          <h3 class={heading}>{t.view}</h3>
          <div class="grid grid-cols-2 gap-3">
            <label class="flex flex-col gap-1">
              <span class="font-medium">{t.gridZoom}</span>
              <input
                class={field}
                type="number"
                min={gridRange.min}
                max={gridRange.max}
                step="10"
                value={values.gridZoom}
                onchange={(e) => {
                  const zoom = Number(e.currentTarget.value);
                  if (Number.isFinite(zoom)) editor.zoomGrid(zoom);
                  e.currentTarget.value = String(settings.values.gridZoom);
                }}
              />
            </label>
            <label class="flex flex-col gap-1">
              <span class="font-medium">{t.pageZoom}</span>
              <select
                class={field}
                value={String(values.pageZoom)}
                onchange={(e) => {
                  const v = e.currentTarget.value;
                  settings.update({ pageZoom: v === 'fit' ? 'fit' : Number(v) });
                }}
              >
                <option value="fit">{t.fit}</option>
                {#each [50, 75, 100, 125, 150, 200] as zoom (zoom)}
                  <option value={String(zoom)}>{zoom}%</option>
                {/each}
              </select>
            </label>
          </div>
        </section>

        <section class={section}>
          <h3 class={heading}>{t.annotations}</h3>
          <label class="flex flex-col gap-1">
            <span class="font-medium">{t.author}</span>
            <input
              class={field}
              value={values.author}
              autocomplete="name"
              onchange={(e) => settings.update({ author: e.currentTarget.value.trim() })}
            />
            <span class="text-xs text-muted-foreground">{t.authorHint}</span>
          </label>
          {@render swatches('highlightColor', t.highlightColor, t.highlightHint)}
          {@render swatches('drawColor', t.drawColor, t.drawHint)}
          <label class="flex w-40 flex-col gap-1">
            <span class="font-medium">{t.fontSize}</span>
            <select
              class={field}
              value={values.fontSize}
              onchange={(e) => {
                const size = Number(e.currentTarget.value);
                settings.update({ fontSize: size });
                editor.fontSize = size;
              }}
            >
              {#each fontSizes as size (size)}
                <option value={size}>{size} pt</option>
              {/each}
            </select>
          </label>
        </section>

        <section class={section}>
          <h3 class={heading}>{t.digitalSignature}</h3>
          <div class="grid grid-cols-2 gap-3">
            <label class="flex flex-col gap-1">
              <span class="font-medium">{t.reason}</span>
              <input
                class={field}
                value={values.signReason}
                onchange={(e) => settings.update({ signReason: e.currentTarget.value.trim() })}
              />
            </label>
            <label class="flex flex-col gap-1">
              <span class="font-medium">{t.location}</span>
              <input
                class={field}
                value={values.signLocation}
                onchange={(e) => settings.update({ signLocation: e.currentTarget.value.trim() })}
              />
            </label>
          </div>
          {#if isDesktop}
            <h4 class="font-medium">{t.smartCard}</h4>
            <label class="flex flex-col gap-1">
              <span>{t.module}</span>
              <input
                class={field}
                spellcheck="false"
                value={values.pkcs11Module}
                onchange={(e) => settings.update({ pkcs11Module: e.currentTarget.value.trim() })}
              />
            </label>
            <label class="flex flex-col gap-1">
              <span>{t.tsa}</span>
              <input
                class={field}
                spellcheck="false"
                placeholder="https://…"
                value={values.tsaUrl}
                onchange={(e) => settings.update({ tsaUrl: e.currentTarget.value.trim() })}
              />
            </label>
            <label class="flex items-center gap-2">
              <input
                type="checkbox"
                checked={values.longTerm}
                onchange={(e) => settings.update({ longTerm: e.currentTarget.checked })}
              />
              {t.longTerm}
            </label>
          {/if}
        </section>

        <section class={section} id="settings-signatures" data-testid="graphic-signatures">
          <h3 class="flex items-baseline justify-between {heading}">
            {g.title}
            <span class="text-xs font-normal text-muted-foreground"
              >{g.count(graphicSignatures.list.length, maxSignatures)}</span
            >
          </h3>
          <p class="text-xs text-muted-foreground">{g.about}</p>
          {#if graphicSignatures.list.length === 0}
            <p class="text-muted-foreground">{g.none}</p>
          {:else}
            <ul class="grid grid-cols-2 gap-2">
              {#each graphicSignatures.list as s (s.id)}
                <li class="flex items-center gap-2 rounded-md border p-2">
                  <span class="flex h-14 flex-1 items-center justify-center rounded bg-white">
                    <img {@attach showPicture(s.png)} alt={s.name} class="max-h-12 max-w-full" />
                  </span>
                  <button
                    type="button"
                    class="flex size-8 items-center justify-center rounded-md text-muted-foreground hover:bg-muted hover:text-foreground"
                    aria-label={g.remove(s.name)}
                    title={g.remove(s.name)}
                    onclick={() => graphicSignatures.remove(s.id)}><Trash2 class="size-4" /></button
                  >
                </li>
              {/each}
            </ul>
          {/if}
          {#if graphicSignatures.list.length < maxSignatures}
            <div class="flex flex-wrap items-center gap-3">
              <label
                class="flex cursor-pointer items-center gap-2 rounded-md border border-dashed px-3 py-2 focus-within:ring-2 focus-within:ring-emphasis hover:bg-muted"
              >
                <ImagePlus class="size-4" />
                {g.add}
                <input
                  type="file"
                  accept="image/png,image/jpeg,image/webp"
                  class="sr-only"
                  data-testid="graphic-signature-file"
                  disabled={adding}
                  onchange={(e) => {
                    const file = e.currentTarget.files?.[0];
                    e.currentTarget.value = '';
                    if (file) add(file);
                  }}
                />
              </label>
              <label class="flex items-center gap-2">
                <input type="checkbox" bind:checked={whiteToTransparent} />
                {g.whiteToTransparent}
              </label>
            </div>
          {/if}
          {#if problem}
            <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">{problem}</p>
          {/if}
        </section>
      </div>

      <div class="-mx-6 flex justify-between gap-2 border-t px-6 pt-4">
        <button
          type="button"
          class={secondaryButton}
          onclick={() => {
            if (!confirm(t.resetConfirm)) return;
            settings.reset();
            editor.zoomGrid(settings.values.gridZoom);
            editor.highlightColor = [...settings.values.highlightColor];
            editor.drawColor = [...settings.values.drawColor];
            editor.fontSize = settings.values.fontSize;
          }}>{t.reset}</button
        >
        <button type="button" class={secondaryButton} onclick={() => (editor.settingsOpen = false)}>{t.close}</button>
      </div>
    </div>
  {/if}
</dialog>
