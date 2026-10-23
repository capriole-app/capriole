<script lang="ts">
  // A screenshot of the app in a window's frame: the page's language, the theme's version (the pictures:
  // shots/shots.spec.ts, npm run shots). Loaded when it comes near the screen; its size declared, so that
  // nothing moves when it arrives.
  import { i18n } from '$lib/i18n/index.svelte';

  let { scene, alt }: { scene: string; alt: string } = $props();
  const src = (theme: string) => `/landing/${i18n.locale}/${scene}-${theme}.webp`;
</script>

<div class="overflow-hidden rounded-xl border bg-card shadow-[0_28px_70px_-34px_rgb(17_29_74/0.38)]">
  <div class="flex h-8 items-center gap-1.5 border-b bg-background px-3" aria-hidden="true">
    <span class="size-2.5 rounded-full bg-border"></span><span class="size-2.5 rounded-full bg-border"></span><span
      class="size-2.5 rounded-full bg-border"
    ></span>
  </div>
  <img
    src={src('light')}
    {alt}
    width="1600"
    height="1000"
    loading="lazy"
    decoding="async"
    class="block h-auto w-full dark:hidden"
  />
  <img
    src={src('dark')}
    {alt}
    width="1600"
    height="1000"
    loading="lazy"
    decoding="async"
    class="hidden h-auto w-full dark:block"
  />
</div>
