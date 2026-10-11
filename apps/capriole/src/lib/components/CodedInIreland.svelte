<script lang="ts">
  // The «coded in Ireland» badge (its light and dark versions, as the theme is).
  let { class: className = 'h-7' }: { class?: string } = $props();
</script>

<img
  src="/coded-in-ireland.svg"
  alt="Coded in Ireland"
  class={[className, 'w-auto dark:hidden']}
  data-testid="coded-in-ireland"
/>
<img src="/coded-in-ireland-dark.svg" alt="Coded in Ireland" class={[className, 'hidden w-auto dark:block']} />
