<script lang="ts">
  // Not a byte of the document leaves the computer: and the open code is how one can check it.
  import Code from '@lucide/svelte/icons/code-xml';
  import ShieldCheck from '@lucide/svelte/icons/shield-check';
  import UserX from '@lucide/svelte/icons/user-x';
  import Heading from './Heading.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { landingWords } from '$lib/i18n/landing';

  const t = $derived(tr(landingWords).privacy);
  const points = $derived([
    [ShieldCheck, t.points.upload],
    [Code, t.points.open],
    [UserX, t.points.account],
  ] as const);
</script>

<section id="privacy" aria-labelledby="privacy-title" class="scroll-mt-16 bg-card py-20 sm:py-28">
  <div class="mx-auto grid max-w-6xl items-center gap-12 px-4 sm:px-8 lg:grid-cols-2 lg:gap-16">
    <div>
      <Heading id="privacy-title" eyebrow={t.eyebrow} title={t.title} />
      <p class="mt-5 max-w-xl text-lg text-muted-foreground">{t.lead}</p>
    </div>
    <ul class="grid gap-6">
      {#each points as [Icon, point] (point.title)}
        <li class="flex items-start gap-4">
          <span class="flex size-9 shrink-0 items-center justify-center rounded-lg bg-success-background text-success">
            <Icon class="size-5" aria-hidden="true" />
          </span>
          <span>
            <span class="block font-semibold">{point.title}</span>
            <span class="block text-[0.9375rem] text-muted-foreground">{point.text}</span>
          </span>
        </li>
      {/each}
    </ul>
  </div>
</section>
