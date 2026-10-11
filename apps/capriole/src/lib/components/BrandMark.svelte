<script lang="ts">
  // Capriole's mark (the arrow around the sheet) at the start of a bar: a button that opens «About».
  import { getEditor } from '$lib/editor.svelte';

  let { label }: { label: string } = $props();
  const editor = getEditor();
</script>

<button
  type="button"
  class="flex size-9 shrink-0 items-center justify-center rounded-md hover:bg-muted"
  aria-label={label}
  title={label}
  data-testid="brand-mark"
  onclick={() => (editor.licensesOpen = true)}
>
  <img src="/mark.svg" alt="" class="size-7 dark:hidden" />
  <img src="/mark-dark.svg" alt="" class="hidden size-7 dark:block" />
</button>
