<script lang="ts">
  // Beside the pages: the document's signatures, as the engine verifies them — whether what was signed is
  // unchanged, whether changes came after, who signed and when, where it is shown —, and the way to sign.
  import ShieldAlert from '@lucide/svelte/icons/shield-alert';
  import ShieldCheck from '@lucide/svelte/icons/shield-check';
  import ShieldX from '@lucide/svelte/icons/shield-x';
  import Signature from '@lucide/svelte/icons/signature';
  import X from '@lucide/svelte/icons/x';
  import type { SignatureInfo } from '@capriole/engine-client';
  import { getEditor } from '$lib/editor.svelte';
  import { commonName } from '$lib/certificates';
  import { localDateTime } from '$lib/files';
  import { tr } from '$lib/i18n/index.svelte';
  import { signaturesWords } from '$lib/i18n/signatures';

  const editor = getEditor();
  const t = $derived(tr(signaturesWords));

  function status(s: SignatureInfo): { text: string; tone: 'success' | 'warning' | 'danger' | 'muted' } {
    if (s.integrity === 'invalid') return { text: t.invalid, tone: 'danger' };
    if (s.integrity === 'unsupported') return { text: t.unsupported, tone: 'muted' };
    if (!s.coversWholeFile) return { text: t.changedAfter, tone: 'warning' };
    return { text: s.documentTimestamp ? t.timestampValid : t.valid, tone: 'success' };
  }

  const tones = {
    success: 'bg-success-background text-success',
    warning: 'bg-warning-background text-warning',
    danger: 'bg-danger-background text-danger',
    muted: 'bg-muted text-muted-foreground',
  };
  const button =
    'flex items-center gap-1.5 rounded-md border px-2.5 py-1.5 text-sm hover:bg-muted disabled:pointer-events-none disabled:opacity-50';
</script>

<aside
  class="flex w-80 shrink-0 flex-col gap-4 overflow-y-auto border-s bg-card p-4 text-sm"
  aria-label={t.title}
  data-testid="signatures-panel"
>
  <header class="flex items-center justify-between">
    <h2 class="flex items-center gap-2 font-semibold"><Signature class="size-4" /> {t.title}</h2>
    <button
      type="button"
      class="flex size-8 items-center justify-center rounded-md hover:bg-muted"
      aria-label={t.close}
      onclick={() => (editor.signaturesOpen = false)}><X class="size-4" /></button
    >
  </header>

  {#if editor.signatures.length === 0}
    <p class="text-muted-foreground">{t.unsigned}</p>
  {/if}
  <ul class="flex flex-col gap-2" data-testid="signature-list">
    {#each editor.signatures as s (`${s.field[0]} ${s.field[1]}`)}
      {@const st = status(s)}
      <li class="flex flex-col gap-1.5 rounded-md border p-2">
        <span class={['flex items-center gap-1.5 rounded px-2 py-1 font-medium', tones[st.tone]]}>
          {#if st.tone === 'success'}
            <ShieldCheck class="size-4 shrink-0" />
          {:else if st.tone === 'danger'}
            <ShieldX class="size-4 shrink-0" />
          {:else}
            <ShieldAlert class="size-4 shrink-0" />
          {/if}
          {st.text}
        </span>
        {#if s.integrity !== 'valid' && s.problem}
          <span class="text-xs text-muted-foreground">{s.problem}</span>
        {/if}
        <span class="font-medium">{s.signer?.name || s.name || s.fieldName}</span>
        {#if s.signer}
          <span class="text-xs text-muted-foreground">{t.issuedBy(commonName(s.signer.issuer))}</span>
        {/if}
        {#if s.signingTime}
          <span class="text-xs">
            {s.documentTimestamp ? t.dated : t.signedOn}
            {localDateTime(s.signingTime)}
            <span class="text-muted-foreground">({s.timestamp ? t.byTimestamp : t.bySigner})</span>
          </span>
        {/if}
        {#if s.timestampInvalid}
          <span class="text-xs text-warning">{t.timestampInvalid}</span>
        {/if}
        {#if s.revocation === 'revoked'}
          <span class="text-xs text-danger">{t.revoked(s.revokedAt ? localDateTime(s.revokedAt) : null)}</span>
        {:else if s.revocation === 'good'}
          <span class="text-xs">{t.notRevoked(s.revocationInFile)}</span>
        {:else if s.revocation === 'unknown'}
          <span class="text-xs text-warning">{t.revocationUnknown}</span>
        {/if}
        {#if s.signer && s.signingTime && !s.certificateValidAtSigning}
          <span class="text-xs text-warning">{t.notValidAtSigning}</span>
        {/if}
        {#if s.reason || s.location}
          <span class="text-xs"
            >{[s.reason && t.reason(s.reason), s.location && t.location(s.location)].filter(Boolean).join(' · ')}</span
          >
        {/if}
        <span class="text-xs text-muted-foreground">
          {[s.level && `PAdES ${s.level}`, s.subFilter, s.digest].filter(Boolean).join(' · ')}
        </span>
        {#if s.page !== null}
          <button type="button" class={[button, 'self-start']} onclick={() => editor.view(s.page!)}>
            {t.goToPage(s.page + 1)}
          </button>
        {/if}
      </li>
    {/each}
  </ul>
  {#if editor.signatures.some((s) => s.signer)}
    <p class="text-xs text-muted-foreground">
      {t.noTrustList}
    </p>
  {/if}

  <button
    type="button"
    class={[button, 'self-start']}
    disabled={editor.busy}
    onclick={() => editor.requestSignature(editor.viewing ?? 0, null)}
  >
    <Signature class="size-4" />
    {t.sign}
  </button>
  <p class="text-xs text-muted-foreground">
    {t.invisibleHint}
  </p>
</aside>
