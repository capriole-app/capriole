<script lang="ts">
  // What Capriole does, in nine lines: for those who scan the page, and for search engines.
  import Archive from '@lucide/svelte/icons/archive';
  import ClipboardPen from '@lucide/svelte/icons/clipboard-pen-line';
  import Eraser from '@lucide/svelte/icons/eraser';
  import FilePen from '@lucide/svelte/icons/file-pen-line';
  import Files from '@lucide/svelte/icons/files';
  import Highlighter from '@lucide/svelte/icons/highlighter';
  import Paperclip from '@lucide/svelte/icons/paperclip';
  import Signature from '@lucide/svelte/icons/signature';
  import Wrench from '@lucide/svelte/icons/wrench';
  import Heading from './Heading.svelte';
  import { tr } from '$lib/i18n/index.svelte';
  import { landingWords } from '$lib/i18n/landing';

  const t = $derived(tr(landingWords).features);
  const icons = {
    text: FilePen,
    pages: Files,
    annotations: Highlighter,
    forms: ClipboardPen,
    signature: Signature,
    redaction: Eraser,
    pdfa: Archive,
    attachments: Paperclip,
    repair: Wrench,
  } as const;
  const keys = Object.keys(icons) as (keyof typeof icons)[];
</script>

<section id="features" aria-labelledby="features-title" class="scroll-mt-16 bg-card py-20 sm:py-28">
  <div class="mx-auto max-w-6xl px-4 sm:px-8">
    <Heading id="features-title" eyebrow={t.eyebrow} title={t.title} />
    <ul class="mt-12 grid gap-x-12 gap-y-9 sm:grid-cols-2 lg:grid-cols-3">
      {#each keys as key (key)}
        {@const Icon = icons[key]}
        <li>
          <span class="mb-1 flex items-center gap-2.5 font-semibold">
            <Icon class="size-5 text-emphasis" aria-hidden="true" />{t.items[key].title}
          </span>
          <span class="block text-[0.9375rem] text-muted-foreground">{t.items[key].text}</span>
        </li>
      {/each}
    </ul>
  </div>
</section>
