<script lang="ts">
  // Where Capriole comes from: written from scratch, its engine and its interface. On the logo's ink, in
  // both themes. What it says is in the brief's «Affermazioni»: the open source libraries it does use are
  // named, in «Informazioni e licenze».
  import Heading from './Heading.svelte';
  import { getApp } from '$lib/editor-context';
  import { tr } from '$lib/i18n/index.svelte';
  import { landingWords } from '$lib/i18n/landing';
  import { repository } from '$lib/releases';

  const app = getApp();
  const t = $derived(tr(landingWords).scratch);
  const button =
    'inline-flex h-11 items-center rounded-md border border-white/35 px-4 text-[0.9375rem] font-medium text-white hover:bg-white/10';
</script>

<section
  id="from-scratch"
  aria-labelledby="from-scratch-title"
  class="scroll-mt-16 bg-ink py-20 text-[#c4c7d4] sm:py-28"
>
  <div class="mx-auto max-w-6xl px-4 sm:px-8">
    <Heading id="from-scratch-title" eyebrow={t.eyebrow} title={t.title} dark />
    <p class="mt-5 max-w-3xl text-lg">{t.lead}</p>
    <ul class="mt-14 grid gap-10 md:grid-cols-3 md:gap-12">
      {#each [t.points.engine, t.points.interface, t.points.license] as point (point.title)}
        <li>
          <span class="mb-1 block font-semibold text-white">{point.title}</span>
          <span class="block text-[0.9375rem]">{point.text}</span>
        </li>
      {/each}
    </ul>
    <div class="mt-12 flex flex-wrap gap-3">
      <a href={repository} class={button} target="_blank" rel="noopener noreferrer">{t.github}</a>
      <button
        type="button"
        class={button}
        onclick={() => {
          if (app.editor) app.editor.licensesOpen = true;
        }}>{t.licenses}</button
      >
    </div>
  </div>
</section>
