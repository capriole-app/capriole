<script lang="ts">
  // A section's heading: a small line above it, in the accent, and the title in the book serif.
  let { id, eyebrow, title, dark = false }: { id: string; eyebrow: string; title: string; dark?: boolean } = $props();
</script>

<p
  class={['mb-3 text-[0.8125rem] font-semibold tracking-[0.08em] uppercase', dark ? 'text-[#8fa5ff]' : 'text-emphasis']}
>
  {eyebrow}
</p>
<h2
  {id}
  class={[
    'max-w-3xl font-serif text-[1.95rem] leading-[1.14] font-semibold tracking-[-0.015em] text-balance sm:text-[2.625rem]',
    dark && 'text-white',
  ]}
>
  {title}
</h2>
