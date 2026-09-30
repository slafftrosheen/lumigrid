import type { TimelineTrack, TimelineStep } from '../api/timeline';
import { MAX_STEPS_PER_TRACK } from '../api/timeline';

export interface StepClipboardItem {
  type: 'step';
  step: TimelineStep;
}

export interface TrackClipboardItem {
  type: 'track';
  enabled: boolean;
  steps: TimelineStep[];
}

let stepClipboard: TimelineStep | null = null;
let trackClipboard: TrackClipboardItem | null = null;

export function copyStep(step: TimelineStep): void {
  stepClipboard = { ...step };
}

export function getCopiedStep(): TimelineStep | null {
  return stepClipboard ? { ...stepClipboard } : null;
}

export function hasCopiedStep(): boolean {
  return stepClipboard !== null;
}

export function pasteStepAfter(
  track: TimelineTrack,
  afterIndex?: number
): TimelineTrack {
  if (!stepClipboard) return track;
  if (track.steps.length >= MAX_STEPS_PER_TRACK) return track;

  const steps = [...track.steps];
  const insertIndex =
    afterIndex !== undefined && afterIndex >= 0 && afterIndex < steps.length
      ? afterIndex + 1
      : steps.length;

  steps.splice(insertIndex, 0, { ...stepClipboard });
  return {
    ...track,
    steps,
    numSteps: steps.length,
    enabled: true,
  };
}

export function copyTrackPattern(track: TimelineTrack): void {
  trackClipboard = {
    type: 'track',
    enabled: track.enabled,
    steps: track.steps.map((s) => ({ ...s })),
  };
}

export function getCopiedTrack(): TrackClipboardItem | null {
  if (!trackClipboard) return null;
  return {
    type: 'track',
    enabled: trackClipboard.enabled,
    steps: trackClipboard.steps.map((s) => ({ ...s })),
  };
}

export function hasCopiedTrack(): boolean {
  return trackClipboard !== null;
}

/**
 * Pastes the copied track pattern onto the target track.
 * Critical invariant: Destination row's channelId is strictly preserved!
 */
export function pasteTrackPattern(targetTrack: TimelineTrack): TimelineTrack {
  if (!trackClipboard) return targetTrack;

  const steps = trackClipboard.steps.slice(0, MAX_STEPS_PER_TRACK).map((s) => ({ ...s }));
  return {
    channelId: targetTrack.channelId, // STRICTLY DESTINATION CHANNEL
    numSteps: steps.length,
    enabled: trackClipboard.enabled && steps.length > 0,
    steps,
  };
}

export function clearClipboard(): void {
  stepClipboard = null;
  trackClipboard = null;
}
