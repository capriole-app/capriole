<script lang="ts">
  // The first screen: a card to drop or choose a PDF. Nothing leaves the browser.
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import ThemeMenu from './ThemeMenu.svelte';
  import { getInspector } from '$lib/inspector.svelte';

  const inspector = getInspector();
  let dragging = $state(false);
  let input: HTMLInputElement | undefined = $state();

  function take(files: FileList | null | undefined): void {
    const file = files?.[0];
    if (file) inspector.open(file);
  }
</script>

<main class="flex min-h-dvh items-center justify-center bg-muted px-6 py-10">
  <div
    role="region"
    aria-label="Apri un PDF"
    class={[
      'relative w-full max-w-md rounded-xl border bg-card p-6 shadow-sm sm:p-8',
      dragging && 'ring-2 ring-primary',
    ]}
    ondragover={(e) => {
      e.preventDefault();
      dragging = true;
    }}
    ondragleave={() => (dragging = false)}
    ondrop={(e) => {
      e.preventDefault();
      dragging = false;
      take(e.dataTransfer?.files);
    }}
  >
    <div class="absolute end-3 top-3"><ThemeMenu /></div>
    <header class="mt-10 mb-8 flex flex-col items-center gap-3 text-center">
      <img src="/logo-light.svg" alt="Capriole" class="h-14 dark:hidden" />
      <img src="/logo-dark.svg" alt="Capriole" class="hidden h-14 dark:block" />
      <p class="text-sm text-muted-foreground">Inspector — il motore al lavoro, oggetto per oggetto</p>
    </header>

    {#if inspector.error}
      <p class="mb-4 rounded-md bg-danger-background px-3 py-2 text-sm text-danger" role="alert">{inspector.error}</p>
    {/if}

    <button
      type="button"
      class="flex w-full items-center justify-center gap-2 rounded-md bg-primary px-4 py-2.5 text-base font-medium text-primary-foreground disabled:opacity-60"
      disabled={inspector.busy}
      onclick={() => input?.click()}
    >
      <FolderOpen class="size-5" />
      {inspector.busy ? 'Apertura…' : 'Scegli un PDF'}
    </button>
    <input
      bind:this={input}
      type="file"
      accept="application/pdf,.pdf"
      class="hidden"
      onchange={(e) => take(e.currentTarget.files)}
    />
    <p class="mt-4 text-center text-sm text-muted-foreground">
      …oppure trascinalo qui. Il file resta nel browser: nessun byte va su un server.
    </p>
    <label class="mt-6 flex items-center justify-center gap-2 text-sm text-muted-foreground">
      <input
        type="checkbox"
        checked={inspector.strict}
        onchange={(e) => (inspector.strict = e.currentTarget.checked)}
      />
      lettura rigorosa (senza riparazioni)
    </label>
    {#if inspector.engineVersion}
      <p class="mt-6 text-center text-xs text-muted-foreground">
        motore Capriole {inspector.engineVersion} (WebAssembly)
      </p>
    {/if}
  </div>
</main>
