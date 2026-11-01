<script lang="ts">
  import { onDestroy } from 'svelte';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';
  import type { TimelineDocument } from '$lib/api/timeline';
  import { getChannelPort } from '$lib/api/timeline';
  import { parseTimelineDocument, validateTimelineDocument, type MissingSceneReference } from '$lib/timeline/validation';

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
  let reading = $state(false);
  let reader: FileReader | null = null;
  let readGeneration = 0;
  let disposed = false;

  function cancelRead() {
    ++readGeneration;
    reader?.abort();
    reader = null;
    reading = false;
  }

  function handleFileSelected(e: Event) {
    const input = e.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;
    cancelRead();
    parsedDoc = null;
    validationErrors = [];
    missingScenes = [];
    reading = true;
    const generation = readGeneration;
    const selectedReader = new FileReader();
    reader = selectedReader;
    selectedReader.onload = () => {
      if (disposed || generation !== readGeneration) return;
      reading = false;
      reader = null;
      jsonText = typeof selectedReader.result === 'string' ? selectedReader.result : '';
      validateInput();
    };
    selectedReader.onerror = () => {
      if (disposed || generation !== readGeneration) return;
      reading = false;
      reader = null;
      validationErrors = ['Could not read this file. Choose it again or paste the JSON.'];
    };
    selectedReader.readAsText(file);
  }

  function validateInput() {
    validationErrors = [];
    missingScenes = [];
    parsedDoc = null;
    if (!jsonText.trim()) {
      validationErrors = ['Choose a timeline file or paste JSON.'];
      return;
    }
    try {
      const doc = parseTimelineDocument(JSON.parse(jsonText));
      const result = validateTimelineDocument(doc, existingSceneSlots);
      validationErrors = result.errors;
      missingScenes = result.missingScenes;
      parsedDoc = doc;
    } catch (error) {
      validationErrors = [error instanceof Error ? error.message : 'Invalid timeline document.'];
    }
  }

  function handleTextInput() {
    cancelRead();
    validateInput();
  }

  function handleConfirmImport() {
    if (disposed || reading || !parsedDoc || validationErrors.length > 0) return;
    onImportSuccess($state.snapshot(parsedDoc));
    onClose();
  }

  onDestroy(() => { disposed = true; cancelRead(); });
</script>

<OperatorDialog open title="Import Timeline Document" description="Import replaces this browser draft. Nothing is written to the node until you save." oncancel={onClose}>
  <div class="import-fields">
    <label for="timeline-import-file">Timeline JSON file</label>
    <input id="timeline-import-file" type="file" accept=".json,application/json" onchange={handleFileSelected} />
    <label for="timeline-import-json">Or paste timeline JSON</label>
    <textarea id="timeline-import-json" bind:value={jsonText} oninput={handleTextInput} rows="8" spellcheck="false"></textarea>
    {#if reading}<p role="status">Reading file…</p>{/if}
    {#if missingScenes.length > 0}
      <div class="warning" role="status">
        <strong>{missingScenes.length} blocks reference missing scenes</strong>
        <ul>
          {#each missingScenes as item}
            <li>{getChannelPort(item.channel)} · Block {item.step + 1}: Scene S{item.presetIndex + 1}</li>
          {/each}
        </ul>
        <p>You can import this draft. Replace or remove missing scenes before saving.</p>
      </div>
    {/if}
    {#if validationErrors.length > 0}
      <div class="error" role="alert">{#each validationErrors as error}<p>{error}</p>{/each}</div>
    {/if}
  </div>
  <div class="actions">
    <button type="button" data-dialog-cancel class="secondary" onclick={onClose}>Cancel</button>
    <button type="button" class="primary" onclick={handleConfirmImport} disabled={reading || !parsedDoc || validationErrors.length > 0}>Import as draft</button>
  </div>
</OperatorDialog>

<style>
  .import-fields { display: flex; flex-direction: column; gap: 8px; min-width: 0; max-height: 55vh; overflow-y: auto; }
  label { color: var(--dash-text); font-size: .85rem; }
  input, textarea { width: 100%; min-width: 0; box-sizing: border-box; color: var(--dash-text); }
  textarea { background: var(--dash-surface); border: 1px solid var(--dash-border); border-radius: var(--dash-radius-sm); padding: 12px; font-family: monospace; resize: vertical; }
  textarea:focus-visible, button:focus-visible { outline: 2px solid var(--dash-accent); outline-offset: 2px; }
  .warning { color: var(--dash-warm); font-size: .8rem; }
  .error { color: var(--dash-danger); font-size: .8rem; overflow-wrap: anywhere; }
  p { margin: 4px 0; }
  ul { padding-left: 20px; }
  .actions { display: flex; justify-content: flex-end; flex-wrap: wrap; gap: 12px; }
  button { min-height: var(--dash-control-h); padding: 8px 16px; border-radius: var(--dash-radius-sm); border: 1px solid var(--dash-border); color: var(--dash-text); cursor: pointer; }
  button:disabled { opacity: .5; cursor: not-allowed; }
  .secondary { background: var(--dash-surface); }
  .primary { background: var(--dash-accent); }
  .primary:hover:not(:disabled) { background: var(--dash-accent-hover); }
</style>
