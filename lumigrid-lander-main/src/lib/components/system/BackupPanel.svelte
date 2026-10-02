<script lang="ts">
  import { saveBackupFile } from '$lib/api/recovery';

  interface Props {
    nodeIp: string;
    /** Immutable node ID — used for filename. Never uses IP in filename. */
    nodeId: string;
  }

  let { nodeIp, nodeId }: Props = $props();

  let downloading = $state(false);
  let error = $state('');
  let toast = $state('');

  async function handleDownload() {
    if (downloading) return;
    downloading = true;
    error = '';
    toast = '';
    try {
      // saveBackupFile uses apiFetch (not apiJson) — backup is streaming .lgbak, not JSON.
      // Filename uses immutable nodeId, never the IP address.
      await saveBackupFile(nodeIp, nodeId);
      toast = 'Backup downloaded successfully.';
      setTimeout(() => { if (toast) toast = ''; }, 4000);
    } catch (e) {
      error = `Backup failed: ${e instanceof Error ? e.message : String(e)}`;
    } finally {
      downloading = false;
    }
  }
</script>

<div class="backup-panel">
  <div class="panel-header">
    <h2>Download Backup</h2>
    <p class="description">
      Downloads a complete backup package of this node's configuration and show data as a
      <code>.lgbak</code> file.
    </p>
  </div>

  <div class="secrets-note">
    <strong>The backup does not include:</strong>
    <ul>
      <li>Authentication credentials (passwords)</li>
      <li>Wi-Fi password</li>
      <li>Sync secret</li>
    </ul>
    <span class="secrets-note-sub">
      These are excluded by design. Restore will preserve your existing credentials.
    </span>
  </div>

  {#if error}
    <div class="error-notice" role="alert">{error}</div>
  {/if}

  {#if toast}
    <div class="success-notice" role="status">{toast}</div>
  {/if}

  <button
    type="button"
    class="download-btn"
    onclick={handleDownload}
    disabled={downloading}
    id="backup-download-btn"
  >
    {#if downloading}
      <span class="spinner" aria-hidden="true"></span>
      Downloading…
    {:else}
      ↓ Download Backup (.lgbak)
    {/if}
  </button>
</div>

<style>
  .backup-panel {
    background: #1a1a1f;
    border: 1px solid #333;
    border-radius: 10px;
    padding: 20px;
    color: #e8e8ed;
    font-family: inherit;
  }

  .panel-header h2 {
    margin: 0 0 6px;
    font-size: 1rem;
    font-weight: 600;
    color: #e8e8ed;
  }

  .description {
    margin: 0 0 16px;
    color: #a8a8b0;
    font-size: 0.88rem;
    line-height: 1.5;
  }

  code {
    background: #2a2a30;
    border-radius: 3px;
    padding: 1px 5px;
    font-family: monospace;
    font-size: 0.85em;
  }

  .secrets-note {
    background: #111115;
    border: 1px solid #2c2c30;
    border-radius: 8px;
    padding: 12px 16px;
    margin-bottom: 16px;
    font-size: 0.85rem;
  }

  .secrets-note strong {
    display: block;
    color: #a8a8b0;
    margin-bottom: 6px;
  }

  .secrets-note ul {
    margin: 0 0 8px;
    padding-left: 18px;
    color: #888;
    line-height: 1.7;
  }

  .secrets-note-sub {
    color: #555;
    font-size: 0.82rem;
  }

  .error-notice {
    background: #2a0e0e;
    border: 1px solid #ff453a;
    border-radius: 8px;
    padding: 10px 13px;
    color: #ff9f9a;
    font-size: 0.87rem;
    margin-bottom: 12px;
  }

  .success-notice {
    background: #0a2015;
    border: 1px solid #1a6030;
    border-radius: 8px;
    padding: 10px 13px;
    color: #30d158;
    font-size: 0.87rem;
    margin-bottom: 12px;
  }

  .download-btn {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 10px 20px;
    border-radius: 8px;
    border: 1px solid #30d158;
    background: #0a2015;
    color: #30d158;
    font-size: 0.92rem;
    font-weight: 500;
    cursor: pointer;
    transition: background 0.15s;
  }

  .download-btn:hover:not(:disabled) {
    background: #0e2e1e;
  }

  .download-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }

  .spinner {
    width: 14px;
    height: 14px;
    border: 2px solid #1a4a2a;
    border-top-color: #30d158;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }

  @keyframes spin {
    to { transform: rotate(360deg); }
  }
</style>
