<script lang="ts">
  interface Props {
    currentChecksum: number | null;
    onReload: () => void;
    onKeepDraft: () => void;
  }

  const { currentChecksum, onReload, onKeepDraft }: Props = $props();
</script>

<div class="modal-backdrop" role="alertdialog" aria-modal="true">
  <div class="modal-panel">
    <div class="modal-icon-badge">⚠️</div>

    <h3 class="modal-title">Timeline Conflict Detected</h3>
    <p class="modal-description">
      The timeline sequence on this node was modified by another operator or session.
      {#if currentChecksum !== null}
        (Remote checksum: <code>0x{currentChecksum.toString(16)}</code>)
      {/if}
      Saving now would overwrite those external changes.
    </p>

    <div class="modal-actions">
      <button type="button" class="btn btn-secondary" onclick={onKeepDraft}>
        Keep My Draft
      </button>
      <button type="button" class="btn btn-primary" onclick={onReload}>
        Reload Node Version
      </button>
    </div>
  </div>
</div>

<style>
  .modal-backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.75);
    backdrop-filter: blur(4px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 110;
    padding: 16px;
  }

  .modal-panel {
    background: #0b0f19;
    border: 1px solid rgba(244, 63, 94, 0.4);
    border-radius: 12px;
    width: 100%;
    max-width: 440px;
    padding: 24px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    box-shadow: 0 20px 40px rgba(0, 0, 0, 0.9);
  }

  .modal-icon-badge {
    font-size: 32px;
    margin-bottom: 12px;
  }

  .modal-title {
    font-size: 16px;
    font-weight: 700;
    color: #f1f5f9;
    margin: 0 0 8px 0;
  }

  .modal-description {
    font-size: 13px;
    color: #94a3b8;
    line-height: 1.5;
    margin: 0 0 20px 0;
  }

  code {
    background: #1e293b;
    padding: 1px 4px;
    border-radius: 3px;
    color: #38bdf8;
    font-family: monospace;
  }

  .modal-actions {
    display: flex;
    gap: 10px;
    width: 100%;
  }

  .btn {
    flex: 1;
    font-size: 12px;
    font-weight: 600;
    padding: 8px 14px;
    border-radius: 6px;
    border: none;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .btn-primary {
    background: #3b82f6;
    color: white;
  }
  .btn-primary:hover {
    background: #2563eb;
  }

  .btn-secondary {
    background: #334155;
    color: #e2e8f0;
  }
  .btn-secondary:hover {
    background: #475569;
  }
</style>
