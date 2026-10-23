<script lang="ts">
  // Six questions (and in step 4 the FAQPage data for search engines, from the same words).
  import Plus from '@lucide/svelte/icons/plus';
  import Heading from './Heading.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { landingWords } from '$lib/i18n/landing';

  const t = $derived(tr(landingWords).faq);
</script>

<section id="faq" aria-labelledby="faq-title" class="scroll-mt-16 py-20 sm:py-28">
  <div class="mx-auto max-w-4xl px-4 sm:px-8">
    <Heading id="faq-title" eyebrow={t.eyebrow} title={t.title} />
    <div class="mt-8 border-t">
      {#each t.items as item (item.q)}
        <details class="group border-b py-5">
          <summary
            class="flex cursor-pointer list-none items-start justify-between gap-4 text-[1.0625rem] font-semibold [&::-webkit-details-marker]:hidden"
          >
            {item.q}
            <Plus
              class="mt-1 size-4 shrink-0 text-emphasis transition-transform group-open:rotate-45"
              aria-hidden="true"
            />
          </summary>
          <p class="mt-3 max-w-3xl text-muted-foreground">{item.a}</p>
        </details>
      {/each}
    </div>
    <p class="mt-16 text-center text-[1.0625rem] text-muted-foreground">
      {t.back}
      <a href="#top" class="text-emphasis underline underline-offset-2">{t.backLink}</a>
    </p>
  </div>
</section>
