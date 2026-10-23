<script lang="ts">
  // The app's screenshots, four tabs: the pictures of the page's language and theme (Shot.svelte). The
  // editor's text is shown large in the hero already: the pages come first here.
  import Heading from './Heading.svelte';
  import Shot from './Shot.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { landingWords } from '$lib/i18n/landing';

  const t = $derived(tr(landingWords).gallery);
  const scenes = ['pages', 'text', 'annotations', 'signature'] as const;
  let chosen = $state<(typeof scenes)[number]>('pages');
  let tabs: HTMLButtonElement[] = $state([]);

  /** The arrows move along the tabs (the tabs pattern of WAI-ARIA). */
  function step(e: KeyboardEvent, i: number): void {
    const to = { ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: scenes.length - 1 }[e.key];
    if (to === undefined) return;
    e.preventDefault();
    const next = (to + scenes.length) % scenes.length;
    chosen = scenes[next]!;
    tabs[next]?.focus();
  }
</script>

<section aria-labelledby="gallery-title" class="py-20 sm:py-28">
  <div class="mx-auto max-w-6xl px-4 sm:px-8">
    <Heading id="gallery-title" eyebrow={t.eyebrow} title={t.title} />
    <div role="tablist" aria-labelledby="gallery-title" class="mt-8 mb-6 flex flex-wrap gap-1.5">
      {#each scenes as scene, i (scene)}
        <button
          bind:this={tabs[i]}
          type="button"
          role="tab"
          id="tab-{scene}"
          aria-selected={chosen === scene}
          aria-controls="shot"
          tabindex={chosen === scene ? 0 : -1}
          class={[
            'h-10 rounded-full border px-4 text-[0.9rem]',
            chosen === scene
              ? 'border-ink bg-ink text-white dark:border-emphasis dark:bg-emphasis'
              : 'bg-card hover:bg-muted',
          ]}
          onclick={() => (chosen = scene)}
          onkeydown={(e) => step(e, i)}>{t.scenes[scene].label}</button
        >
      {/each}
    </div>
    <div role="tabpanel" id="shot" aria-labelledby="tab-{chosen}">
      {#key chosen}
        <Shot scene={chosen} alt={t.scenes[chosen].caption} />
      {/key}
      <p class="mt-4 text-[0.9375rem] text-muted-foreground">{t.scenes[chosen].caption}</p>
    </div>
  </div>
</section>
