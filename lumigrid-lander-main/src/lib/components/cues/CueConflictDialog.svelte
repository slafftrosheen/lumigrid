<script lang="ts">
  /**
   * CueConflictDialog — optimistic concurrency conflict UI (LG-034 §37).
   */
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';

  interface Props {
    open: boolean;
    currentChecksum: number | null;
    busy?: boolean;
    onreload: () => void;
    onkeep: () => void;
  }

  let { open, currentChecksum, busy = false, onreload, onkeep }: Props = $props();
</script>

<OperatorDialog
  {open}
  title="This Cue List changed on the node"
  description="Another operator (or another tab) saved this Cue List first. Your draft was not written to the node. Reload the node version to discard your draft, or keep editing it locally."
  confirmLabel="Reload Node Version"
  cancelLabel="Keep My Draft"
  tone="danger"
  {busy}
  onconfirm={onreload}
  oncancel={onkeep}
>
  {#if currentChecksum !== null}
    <p class="checksum-note">Node checksum: <code>{currentChecksum}</code></p>
  {/if}
</OperatorDialog>

<style>
  .checksum-note {
    font-size: 0.8rem;
    color: rgba(255, 255, 255, 0.55);
  }
  .checksum-note code {
    font-family: monospace;
    color: #ffd60a;
  }
</style>
