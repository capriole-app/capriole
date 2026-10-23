<script lang="ts">
  // The first screen: the headline, the card that opens a PDF (the one call to action), and the editor
  // shown large under them. The struck words and the blue ones are the edit «Modifica testo» makes, drawn
  // once on arrival (still with reduced motion); the struck ones are hidden from screen readers, so the
  // headline reads «Edit PDFs online, the text too». In the desktop shell the card alone, centred.
  import OpenCard from '../OpenCard.svelte';
  import Shot from './Shot.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { landingWords } from '$lib/i18n/landing';

  const t = $derived(tr(landingWords));
</script>

<section
  id="top"
  class="scroll-mt-16 px-4 pt-6 pb-16 sm:px-8 sm:pt-14 sm:pb-24 lg:pt-20 desktop:flex desktop:min-h-dvh desktop:items-center desktop:justify-center desktop:p-6"
>
  <div
    class="mx-auto grid max-w-6xl items-center gap-6 sm:gap-10 lg:grid-cols-[minmax(0,1.3fr)_minmax(0,1fr)] lg:gap-16 desktop:block desktop:w-full desktop:max-w-md"
  >
    <div data-web-only>
      <h1
        class="font-serif text-[2rem] leading-[1.08] font-semibold tracking-[-0.02em] text-balance sm:text-5xl lg:text-[3.5rem]"
      >
        {t.hero.before} <span class="struck" aria-hidden="true">{t.hero.struck}</span>
        <span class="new">{t.hero.after}</span>.
      </h1>
      <p class="mt-3 max-w-xl text-base text-muted-foreground sm:mt-6 sm:text-xl">{t.hero.subtitle}</p>
      <p class="mt-6 hidden text-sm text-muted-foreground sm:block">{t.hero.trust.join(' · ')}</p>
    </div>
    <div class="relative">
      <OpenCard />
      <span
        class="absolute -end-4 -top-5 hidden rotate-3 rounded-md border border-[#efd96a] bg-[#fff6c2] px-3 py-1.5 text-[0.8125rem] text-[#5c4a00] shadow-[0_8px_18px_-10px_rgb(0_0_0/0.3)] lg:block"
        aria-hidden="true"
        data-web-only>{t.card.note}</span
      >
    </div>
  </div>
  <div class="mx-auto mt-16 hidden max-w-6xl sm:block lg:mt-20" data-web-only>
    <Shot scene="text" alt={t.hero.shot} />
  </div>
</section>

<style>
  /* The annotations' own colours: the pen's red (lib/colors.ts), the emphasis blue. */
  .struck {
    color: var(--muted-foreground);
    font-weight: 500;
    text-decoration: line-through;
    text-decoration-color: rgb(230 38 38);
    text-decoration-thickness: 0.07em;
  }
  .new {
    color: var(--emphasis);
    text-decoration: underline;
    text-decoration-color: color-mix(in srgb, var(--emphasis) 35%, transparent);
    text-decoration-thickness: 0.07em;
    text-underline-offset: 0.12em;
  }
  @media (prefers-reduced-motion: no-preference) {
    .struck {
      animation: strike 0.45s ease-out 0.5s both;
    }
    .new {
      animation: write 0.6s ease-out 1.05s both;
    }
  }
  @keyframes strike {
    from {
      color: var(--foreground);
      text-decoration-color: transparent;
    }
  }
  @keyframes write {
    from {
      opacity: 0;
    }
  }
</style>
