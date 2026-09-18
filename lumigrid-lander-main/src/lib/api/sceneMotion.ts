import { apiJson } from './transport';
import type { TransitionCurve } from '../runtime/curves';

export type AutomationTarget =
  | 'brightness'
  | 'speed'
  | 'fx_param'
  | 'layer0_opacity'
  | 'layer1_opacity';

export interface AutomationPoint {
  position: number; // 0..65535
  value: number;    // 0..65535
}

export interface AutomationTrackConfig {
  target: AutomationTarget;
  zone: number;
  layer: number;
  curve: TransitionCurve;
  loop: boolean;
  periodMs: number;
  points: AutomationPoint[];
}

export interface SceneMotionConfig {
  schema_version?: number;
  index: number;
  transitionCurve: TransitionCurve;
  trackCount?: number;
  tracks: AutomationTrackConfig[];
}

export function createDefaultTrack(target: AutomationTarget = 'brightness'): AutomationTrackConfig {
  return {
    target,
    zone: 0,
    layer: 0,
    curve: 'easeInOut',
    loop: true,
    periodMs: 5000,
    points: [
      { position: 0, value: 0 },
      { position: 32768, value: 65535 },
      { position: 65535, value: 0 },
    ],
  };
}

export function createDefaultSceneMotion(sceneIndex: number): SceneMotionConfig {
  return {
    schema_version: 1,
    index: sceneIndex,
    transitionCurve: 'easeInOut',
    trackCount: 0,
    tracks: [],
  };
}

export function getSceneMotion(ip: string, sceneIndex: number): Promise<SceneMotionConfig> {
  return apiJson<SceneMotionConfig>(ip, `/api/v1/scene-motion?index=${sceneIndex}`);
}

export function getAllSceneMotions(ip: string): Promise<SceneMotionConfig[]> {
  return apiJson<SceneMotionConfig[]>(ip, '/api/v1/scene-motion');
}

export function saveSceneMotion(
  ip: string,
  sceneIndex: number,
  motion: SceneMotionConfig
): Promise<{ saved: boolean; commandId?: number }> {
  return apiJson<{ saved: boolean; commandId?: number }>(
    ip,
    `/api/v1/scene-motion?index=${sceneIndex}`,
    {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ...motion,
        index: sceneIndex,
      }),
    }
  );
}
