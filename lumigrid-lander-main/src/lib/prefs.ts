import { readString, writeString } from '$lib/auth/storage';

export const save = (k: string, v: unknown) => {
  try { writeString(k, JSON.stringify(v)); } catch {}
};

export const load = <T>(k: string, d: T): T => {
  try {
    const raw = readString(k);
    return raw ? JSON.parse(raw) as T : d;
  } catch {
    return d;
  }
};
