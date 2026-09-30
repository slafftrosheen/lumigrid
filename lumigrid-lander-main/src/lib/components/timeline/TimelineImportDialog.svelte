<script lang="ts">
  import type { TimelineDocument } from '$lib/api/timeline';
  import { normalizeTimelineDocument, getChannelPort } from '$lib/api/timeline';
  import { validateTimelineDocument, type MissingSceneReference } from '$lib/timeline/validation';

  interface Props {
    existingSceneSlots: Set<number>;
    onImportSuccess: (doc: TimelineDocument) => void;
    onClose: () => void;
  }

  const { existingSceneSlots, onImportSuccess, onClose }: Props = $props();

  let jsonText = $state('');
  let validationErrors = $state<string[]>([]);
  let missingScenes = $state<MissingSceneReference[]>([]);
  let parsedDoc = $state<TimelineDocument | null>(null);

  function handleFileSelected(e: Event) {
    const input = e.target as HTMLInputElement;
    if (!input.files || input.files.length === 0) return;
    const file = input.files[0];
    const reader = new FileReader();
    reader.onload = (event) => {
      jsonText = event.target?.result as string || '';
      validateInput();
    };
    reader.readAsText(file);
  }

  function validateInput() {
    validationErrors = [];
    missingScenes = [];
    parsedDoc = null;

    if (!jsonText.trim()) {
      validationErrors = ['Please select a file or paste JSON'];
      return;
    }

    try {
      const raw = JSON.parse(jsonText);
      const norm = normalizeTimelineDocument(raw);
      const result = validateTimelineDocument(norm, existingSceneSlots);

      if (!result.valid) {
        validationErrors = result.errors;
        missingScenes = result.missingScenes;
      }
      parsedDoc = norm;
    } catch (err: any) {
      validationErrors = [`Invalid JSON syntax: ${err.message}`];
    }
  }

  function handleConfirmImport() {
    if (!parsedDoc) return;
    onImportSuccess(parsedDoc);
    onClose();
  }
</script>

<div class="modal-backdrop" onclick={onClose} role="presentation">
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <div class="modal-panel" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="modal-header">
      <h3 class="modal-title">Import Timeline Document</h3>
      <button type="button" class="close-btn" onclick={onClose}>✕</button>
    </div>

    <div class="modal-body">
      <div class="file-picker-row">
        <label class="file-upload-btn">
          Choose File
          <input type="file" accept=".json" onchange={handleFileSelected} style="display: none;" />
        </label>
        <span class="file-hint">Select a lumigrid-timeline .json document</span>
      </div>

      <textarea
        class="json-textarea"
        placeholder="Or paste JSON sequence document here..."
        bind:value={jsonText}
        oninput={validateInput}
        rows="8"
      ></textarea>

      {#if missingScenes.length > 0}
        <div class="warning-banner">
          <div class="warning-title">⚠️ {missingScenes.length} Blocks Reference Missing Scenes:</div>
          <ul class="missing-list">
            {#each missingScenes as item}
              <li>
                <strong>{getChannelPort(item.channel)}</strong> · Block {item.step + 1} references <strong>Scene S{item.presetIndex + 1}</strong> (not configured on this node)
              </li>
            {/each}
          </ul>
          <p class="warning-footer">
            You may import this as a draft, but you must replace or delete these missing blocks before saving.
          </p>
        </div>
      {/if}

      {#if validationErrors.length > 0}
        <div class="error-banner">
          {#each validationErrors as err}
            <div class="error-item">• {err}</div>
          {/each}
        </div>
      {/if}
    </div>

    <div class="modal-footer">
      <button type="button" class="btn btn-secondary" onclick={onClose}>
        Cancel
      </button>
      <button
        type="button"
        class="btn btn-primary"
        onclick={handleConfirmImport}
        disabled={!parsedDoc || validationErrors.length > 0}
      >
        Import as Draft
      </button>
    </div>
  </div>
</div>

<style>
  .modal-backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.7);
    backdrop-filter: blur(4px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 100;
    padding: 16px;
  }

  .modal-panel {
    background: #0b0f19;
    border: 1px solid #1e293b;
    border-radius: 12px;
    width: 100%;
    max-width: 580px;
    max-height: 85vh;
    display: flex;
    flex-direction: column;
    box-shadow: 0 20px 40px rgba(0, 0, 0, 0.8);
    overflow: hidden;
  }

  .modal-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 16px 20px;
    border-bottom: 1px solid #1e293b;
  }

  .modal-title {
    font-size: 16px;
    font-weight: 700;
    color: #f1f5f9;
    margin: 0;
  }

  .close-btn {
    background: transparent;
    border: none;
    color: #64748b;
    font-size: 16px;
    cursor: pointer;
  }
  .close-btn:hover {
    color: #f1f5f9;
  }

  .modal-body {
    padding: 16px 20px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    overflow-y: auto;
  }

  .file-picker-row {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .file-upload-btn {
    font-size: 12px;
    font-weight: 600;
    background: #1e293b;
    border: 1px solid #334155;
    color: #f1f5f9;
    padding: 6px 14px;
    border-radius: 6px;
    cursor: pointer;
  }
  .file-upload-btn:hover {
    background: #334155;
  }

  .file-hint {
    font-size: 12px;
    color: #94a3b8;
  }

  .json-textarea {
    width: 100%;
    background: #111827;
    border: 1px solid #1f2937;
    border-radius: 6px;
    padding: 10px;
    color: #e2e8f0;
    font-family: monospace;
    font-size: 12px;
    box-sizing: border-box;
    resize: vertical;
  }
  .json-textarea:focus {
    outline: none;
    border-color: #38bdf8;
  }

  .warning-banner {
    background: rgba(245, 158, 11, 0.1);
    border: 1px solid rgba(245, 158, 11, 0.3);
    border-radius: 6px;
    padding: 10px 12px;
  }

  .warning-title {
    font-size: 12px;
    font-weight: 700;
    color: #f59e0b;
    margin-bottom: 6px;
  }

  .missing-list {
    margin: 0;
    padding-left: 18px;
    font-size: 11px;
    color: #fcd34d;
    line-height: 1.5;
  }

  .warning-footer {
    font-size: 11px;
    color: #94a3b8;
    margin: 6px 0 0 0;
  }

  .error-banner {
    background: rgba(239, 68, 68, 0.1);
    border: 1px solid rgba(239, 68, 68, 0.3);
    border-radius: 6px;
    padding: 8px 12px;
  }

  .error-item {
    font-size: 11px;
    color: #f87171;
    line-height: 1.4;
  }

  .modal-footer {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    gap: 10px;
    padding: 12px 20px;
    border-top: 1px solid #1e293b;
    background: #0f172a;
  }

  .btn {
    font-size: 12px;
    font-weight: 600;
    padding: 6px 14px;
    border-radius: 6px;
    border: none;
    cursor: pointer;
  }

  .btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .btn-primary {
    background: #3b82f6;
    color: white;
  }
  .btn-primary:hover:not(:disabled) {
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
