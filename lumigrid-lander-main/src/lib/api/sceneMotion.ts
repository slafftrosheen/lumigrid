import { apiJson } from './transport';
import type { TransitionCurve } from '../runtime/curves';

export type AutomationTarget =
  | 'globalBrightness'
  | 'layerOpacity'
  | 'layerSpeed'
  | 'layerParameter';

type LegacyAutomationTarget =
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
  channel: number; // 0..15 => A1..A8, P1..P8
  layer: number;   // 0..2
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

function normalizeTarget(target: unknown): AutomationTarget {
  switch (target as AutomationTarget | LegacyAutomationTarget) {
    case 'globalBrightness':
    case 'brightness':
      return 'globalBrightness';
    case 'layerOpacity':
    case 'layer0_opacity':
    case 'layer1_opacity':
      return 'layerOpacity';
    case 'layerSpeed':
    case 'speed':
      return 'layerSpeed';
    case 'layerParameter':
    case 'fx_param':
      return 'layerParameter';
    default:
      return 'globalBrightness';
  }
}

function normalizeTrack(raw: any): AutomationTrackConfig {
  const legacyTarget = raw?.target as LegacyAutomationTarget | undefined;
  const inferredLayer =
    legacyTarget === 'layer1_opacity' ? 1 :
    legacyTarget === 'layer0_opacity' ? 0 :
    Number.isFinite(raw?.layer) ? Math.max(0, Math.min(2, Math.round(raw.layer))) : 0;

  const rawChannel = Number.isFinite(raw?.channel)
    ? raw.channel
    : Number.isFinite(raw?.zone)
      ? raw.zone
      : 0;

  return {
    target: normalizeTarget(raw?.target),
    channel: Math.max(0, Math.min(15, Math.round(rawChannel))),
    layer: inferredLayer,
    curve: raw?.curve ?? 'easeInOut',
    loop: raw?.loop !== undefined ? Boolean(raw.loop) : true,
    periodMs: Number.isFinite(raw?.periodMs)
      ? Math.max(100, Math.min(3_600_000, Math.round(raw.periodMs)))
      : 5000,
    points: Array.isArray(raw?.points)
      ? raw.points.map((p: any) => ({
          position: Math.max(0, Math.min(65535, Math.round(Number(p?.position) || 0))),
          value: Math.max(0, Math.min(65535, Math.round(Number(p?.value) || 0))),
        }))
      : [],
  };
}

function normalizeMotion(raw: any, fallbackIndex = 0): SceneMotionConfig {
  const tracks = Array.isArray(raw?.tracks)
    ? raw.tracks.slice(0, 4).map(normalizeTrack)
    : [];

  return {
    schema_version: raw?.schema_version ?? 1,
    index: Number.isFinite(raw?.index) ? Math.round(raw.index) : fallbackIndex,
    transitionCurve: raw?.transitionCurve ?? 'easeInOut',
    trackCount: tracks.length,
    tracks,
  };
}

export function createDefaultTrack(target: AutomationTarget = 'globalBrightness'): AutomationTrackConfig {
  return {
    target,
    channel: 0,
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

export async function getSceneMotion(ip: string, sceneIndex: number): Promise<SceneMotionConfig> {
  const raw = await apiJson<any>(ip, `/api/v1/scene-motion?index=${sceneIndex}`);
  return normalizeMotion(raw, sceneIndex);
}

export async function getAllSceneMotions(ip: string): Promise<SceneMotionConfig[]> {
  const raw = await apiJson<any[]>(ip, '/api/v1/scene-motion');
  return Array.isArray(raw) ? raw.map((m, i) => normalizeMotion(m, i)) : [];
}

export function saveSceneMotion(
  ip: string,
  sceneIndex: number,
  motion: SceneMotionConfig
): Promise<{ saved: boolean; commandId?: number }> {
  const canonical = normalizeMotion(motion, sceneIndex);
  return apiJson<{ saved: boolean; commandId?: number }>(
    ip,
    `/api/v1/scene-motion?index=${sceneIndex}`,
    {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ...canonical,
        index: sceneIndex,
        trackCount: canonical.tracks.length,
      }),
    }
  );
}
