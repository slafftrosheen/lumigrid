import { writable, derived } from 'svelte/store';

// Active sequence store
export const activeSequence = writable(null);

// Playback state
export const playbackState = writable({
  playing: false,
  playhead: 0,
  duration: 0,
  looping: false,
  tempo: 1.0
});

// Sequences list
export const sequences = writable([]);

// Selected tracks (for multi-select operations)
export const selectedTracks = writable([]);

// Timeline view settings
export const timelineView = writable({
  zoom: 1.0,
  scrollX: 0,
  snapToGrid: true,
  quantization: 'QUANT_1_4'
});

// Derived store: Is any sequence active?
export const hasActiveSequence = derived(
  activeSequence,
  $activeSequence => $activeSequence !== null
);

// Derived store: Current playback time in seconds
export const playheadSeconds = derived(
  playbackState,
  $playbackState => $playbackState.playhead / 1000
);

// Actions
export function setActiveSequence(sequence) {
  activeSequence.set(sequence);
  playbackState.update(state => ({
    ...state,
    playhead: 0,
    duration: sequence?.total_duration_ms || 0
  }));
}

export function updatePlaybackState(updates) {
  playbackState.update(state => ({ ...state, ...updates }));
}

export function addSequence(sequence) {
  sequences.update(list => [...list, sequence]);
}

export function removeSequence(id) {
  sequences.update(list => list.filter(seq => seq.id !== id));
  activeSequence.update(seq => seq?.id === id ? null : seq);
}

export function toggleTrackSelection(trackId) {
  selectedTracks.update(tracks => {
    const index = tracks.indexOf(trackId);
    if (index >= 0) {
      return tracks.filter(id => id !== trackId);
    } else {
      return [...tracks, trackId];
    }
  });
}

export function clearTrackSelection() {
  selectedTracks.set([]);
}
