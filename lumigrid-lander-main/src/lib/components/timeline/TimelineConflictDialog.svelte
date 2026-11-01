<script lang="ts">
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';

  interface Props {
    currentChecksum: number | null;
    busy?: boolean;
    error?: string | null;
    onReload: () => void;
    onKeepDraft: () => void;
  }

  const { currentChecksum, busy = false, error = null, onReload, onKeepDraft }: Props = $props();
</script>

<OperatorDialog
  open
  title="Timeline Conflict Detected"
  description="The saved timeline changed while this draft was open. Keep your draft, or load the current node version and replace it."
  tone="warning"
  {busy}
  oncancel={onKeepDraft}
>
  {#if currentChecksum !== null}
    <p class="checksum">Node checksum: <code>0x{currentChecksum.toString(16)}</code></p>
  {/if}
  {#if error}<p class="error" role="alert">{error}</p>{/if}
  <div class="actions">
    <button type="button" data-dialog-cancel class="secondary" disabled={busy} onclick={onKeepDraft}>Keep my draft</button>
    <button type="button" class="primary" disabled={busy} onclick={onReload}>{busy ? 'Loading…' : 'Reload node version'}</button>
  </div>
</OperatorDialog>

<style>
  .checksum { color: var(--dash-muted); margin: 0; font-size: 0.8rem; }
  .error { color: var(--dash-danger); margin: 0; overflow-wrap: anywhere; }
  .actions { display: flex; gap: 12px; flex-wrap: wrap; }
  button { min-height: var(--dash-control-h); padding: 8px 16px; border-radius: var(--dash-radius-sm); border: 1px solid var(--dash-border); color: var(--dash-text); cursor: pointer; }
  button:disabled { opacity: .5; cursor: wait; }
  button:focus-visible { outline: 2px solid var(--dash-accent); outline-offset: 2px; }
  .secondary { background: var(--dash-surface); }
  .primary { background: var(--dash-accent); }
  .primary:hover:not(:disabled) { background: var(--dash-accent-hover); }
</style>
