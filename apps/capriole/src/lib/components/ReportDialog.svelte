<script lang="ts">
  // «Segnala un problema»: what the person writes, and everything else that would be sent, shown before
  // they send it (report.ts). Nothing leaves without the click on «Invia».
  import MessageSquareWarning from '@lucide/svelte/icons/message-square-warning';
  import { getEditor } from '$lib/editor.svelte';
  import { i18n, tr } from '$lib/i18n/index.svelte';
  import { reportWords } from '$lib/i18n/report';
  import { browserOf, reportVersion, send, type Report, type Sent } from '$lib/report';
  import { provider } from '$lib/terms-version';

  const editor = getEditor();
  const t = $derived(tr(reportWords));
  let description = $state('');
  let phase = $state<'writing' | 'sending' | Sent>('writing');
  let missing = $state(false);

  const where = typeof navigator === 'undefined' ? { browser: 'other', os: 'other' } : browserOf(navigator.userAgent);
  const report = $derived<Report>({
    v: reportVersion,
    description: description.trim().slice(0, 2000),
    app: __APP_VERSION__,
    engine: editor.engineVersion,
    platform: 'web',
    browser: where.browser,
    os: where.os,
    language: i18n.locale,
    error: editor.lastError?.slice(0, 500) ?? null,
    document: editor.traits,
  });

  async function submit() {
    if (report.description === '' && !report.error) {
      missing = true;
      return;
    }
    phase = 'sending';
    phase = await send(report);
  }

  function close() {
    editor.reportOpen = false;
    if (phase === 'sent') description = '';
    phase = 'writing';
    missing = false;
  }

  // A modal dialog while it is open (showModal: focus trapped, Escape closes).
  function modal(dialog: HTMLDialogElement) {
    if (editor.reportOpen && !dialog.open) dialog.showModal();
    if (!editor.reportOpen && dialog.open) dialog.close();
  }
</script>

<dialog
  {@attach modal}
  class="m-auto w-full max-w-lg rounded-xl border bg-card p-6 text-foreground shadow-lg backdrop:bg-black/40"
  aria-labelledby="report-title"
  data-testid="report-dialog"
  oncancel={(e) => {
    e.preventDefault();
    close();
  }}
>
  <div class="flex max-h-[85dvh] flex-col gap-4 text-sm">
    <h2 id="report-title" class="flex items-center gap-2 text-base font-semibold">
      <MessageSquareWarning class="size-4" />
      {t.title}
    </h2>

    {#if phase === 'sent'}
      <p class="rounded-md bg-muted px-3 py-2" role="status">{t.sent}</p>
    {:else}
      <div class="flex min-h-0 flex-col gap-3 overflow-y-auto pe-1">
        <p class="text-muted-foreground">{t.intro}</p>
        <label class="flex flex-col gap-1">
          <span class="font-medium">{t.describe}</span>
          <textarea
            bind:value={description}
            oninput={() => (missing = false)}
            maxlength="2000"
            rows="4"
            class="rounded-md border bg-background px-3 py-2 outline-none focus:border-primary"
            placeholder={t.placeholder}
            disabled={phase === 'sending'}></textarea>
        </label>
        {#if missing}
          <p class="text-danger" role="alert">{t.needed}</p>
        {/if}

        <div class="flex flex-col gap-1.5">
          <p class="font-medium">{t.whatIsSent}</p>
          <dl
            class="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 rounded-md bg-muted px-3 py-2 text-xs"
            data-testid="report-sent"
          >
            <dt class="text-muted-foreground">{t.app}</dt>
            <dd>{report.app}</dd>
            <dt class="text-muted-foreground">{t.engine}</dt>
            <dd>{report.engine ?? '—'}</dd>
            <dt class="text-muted-foreground">{t.browser}</dt>
            <dd>{report.browser}, {report.os}</dd>
            <dt class="text-muted-foreground">{t.language}</dt>
            <dd>{report.language}</dd>
            <dt class="text-muted-foreground">{t.error}</dt>
            <dd class="break-words">{report.error ?? t.none}</dd>
            <dt class="text-muted-foreground">{t.document}</dt>
            <dd>
              {report.document
                ? t.traits(
                    report.document.pdfVersion,
                    report.document.pages,
                    report.document.encrypted,
                    report.document.size,
                  )
                : t.noDocument}
            </dd>
            {#if report.document}
              <dt class="text-muted-foreground">{t.repairs}</dt>
              <dd class="break-words">{report.document.repairs.join(', ') || t.noRepairs}</dd>
            {/if}
          </dl>
        </div>

        {#if phase === 'tooMany' || phase === 'failed'}
          <p class="rounded-md bg-danger-background px-3 py-2 text-danger" role="alert">
            {phase === 'tooMany' ? t.tooMany : t.failed(provider.contact)}
          </p>
        {/if}
      </div>
    {/if}

    <div class="flex justify-end gap-2">
      {#if phase === 'sent'}
        <button type="button" class="rounded-md border px-3 py-1.5 hover:bg-muted" onclick={close}>{t.close}</button>
      {:else}
        <button type="button" class="rounded-md border px-3 py-1.5 hover:bg-muted" onclick={close}>{t.cancel}</button>
        <button
          type="button"
          class="rounded-md bg-primary px-3 py-1.5 font-medium text-primary-foreground disabled:opacity-60"
          disabled={phase === 'sending'}
          onclick={submit}>{phase === 'sending' ? t.sending : t.send}</button
        >
      {/if}
    </div>
  </div>
</dialog>
