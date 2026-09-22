import { readString, writeString } from './auth/storage';

export const save = (k: string, v: unknown) => {
  try { writeString(k, JSON.stringify(v)); } catch {}
};
export const load = <T>(k: string, d: T): T => {
  try { const v = readString(k); return v ? JSON.parse(v) as T : d; } catch { return d; }
};

