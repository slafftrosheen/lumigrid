/**
 * Node-authoritative per-node browser session store (LG-029).
 *
 * Sessions are stored in memory and sessionStorage (for tab reload continuity).
 * NEVER in localStorage.
 * No long-lived node access credentials are ever persisted.
 */

export interface NodeAuthSession {
  nodeId: string;
  address: string;
  role: 'operator' | 'installer' | 'admin';
  sessionToken: string;
  expiresAt: number; // Unix ms
}

const SESSION_STORAGE_KEY = 'lg_node_sessions';

function getSessionStorage(): Storage | null {
  if (typeof window !== 'undefined' && window.sessionStorage) {
    return window.sessionStorage;
  }
  if (typeof sessionStorage !== 'undefined') {
    return sessionStorage;
  }
  return null;
}

export function normalizeNodeAddress(address: string): string {
  if (!address) return '';
  let clean = address.trim().toLowerCase();
  clean = clean.replace(/^https?:\/\//, '');
  const colonIdx = clean.indexOf(':');
  if (colonIdx !== -1) {
    clean = clean.substring(0, colonIdx);
  }
  return clean;
}

class NodeAuthSessionStore {
  private sessionsByAddress = new Map<string, NodeAuthSession>();
  private sessionsByNodeId = new Map<string, NodeAuthSession>();
  private addressToNodeId = new Map<string, string>();
  private subscribers = new Set<(sessions: NodeAuthSession[]) => void>();

  constructor() {
    this.loadFromSessionStorage();
  }

  private loadFromSessionStorage(): void {
    const storage = getSessionStorage();
    if (!storage) return;

    try {
      const raw = storage.getItem(SESSION_STORAGE_KEY);
      if (!raw) return;
      const parsed = JSON.parse(raw);
      const list: NodeAuthSession[] = Array.isArray(parsed)
        ? parsed
        : typeof parsed === 'object' && parsed !== null
          ? (Object.values(parsed) as NodeAuthSession[])
          : [];
      const now = Date.now();
      for (const s of list) {
        if (s && s.sessionToken && s.expiresAt > now) {
          const norm = normalizeNodeAddress(s.address);
          this.sessionsByAddress.set(norm, s);
          if (s.nodeId) {
            this.sessionsByNodeId.set(s.nodeId, s);
            this.addressToNodeId.set(norm, s.nodeId);
          }
        }
      }
    } catch {
      // Ignore parse errors from corrupted storage
    }
  }

  public restoreFromStorage(): void {
    this.sessionsByAddress.clear();
    this.sessionsByNodeId.clear();
    this.addressToNodeId.clear();
    this.loadFromSessionStorage();
    this.notify();
  }

  private saveToSessionStorage(): void {
    const storage = getSessionStorage();
    if (!storage) return;

    try {
      const list = Array.from(this.sessionsByAddress.values()).filter(
        (s) => s.expiresAt > Date.now()
      );
      if (list.length > 0) {
        storage.setItem(SESSION_STORAGE_KEY, JSON.stringify(list));
      } else {
        storage.removeItem(SESSION_STORAGE_KEY);
      }
    } catch {
      // Ignore storage errors (quota, disabled, etc.)
    }
  }

  private notify(): void {
    const current = Array.from(this.sessionsByAddress.values());
    for (const sub of this.subscribers) {
      try {
        sub(current);
      } catch {}
    }
  }

  public subscribe(fn: (sessions: NodeAuthSession[]) => void): () => void {
    this.subscribers.add(fn);
    fn(Array.from(this.sessionsByAddress.values()));
    return () => {
      this.subscribers.delete(fn);
    };
  }

  public getSession(key: string): NodeAuthSession | null {
    if (!key) return null;
    const norm = normalizeNodeAddress(key);

    let session = this.sessionsByAddress.get(norm) ?? this.sessionsByNodeId.get(key) ?? null;
    if (!session) {
      const mappedNodeId = this.addressToNodeId.get(norm);
      if (mappedNodeId) {
        session = this.sessionsByNodeId.get(mappedNodeId) ?? null;
      }
    }

    if (session) {
      if (session.expiresAt <= Date.now()) {
        this.clearSession(key);
        return null;
      }
      return session;
    }
    return null;
  }

  public hasValidSession(key: string): boolean {
    return this.getSession(key) !== null;
  }

  public getRole(key: string): 'admin' | 'installer' | 'operator' | 'unknown' {
    const session = this.getSession(key);
    return session ? session.role : 'unknown';
  }

  public setSession(session: NodeAuthSession): void {
    const norm = normalizeNodeAddress(session.address);
    const updated: NodeAuthSession = {
      ...session,
      address: norm,
    };

    this.sessionsByAddress.set(norm, updated);
    if (updated.nodeId) {
      this.sessionsByNodeId.set(updated.nodeId, updated);
      this.addressToNodeId.set(norm, updated.nodeId);
    }

    this.saveToSessionStorage();
    this.notify();
  }

  public bindNodeId(address: string, nodeId: string): void {
    if (!address || !nodeId) return;
    const norm = normalizeNodeAddress(address);
    this.addressToNodeId.set(norm, nodeId);

    const existing = this.sessionsByAddress.get(norm);
    if (existing) {
      existing.nodeId = nodeId;
      this.sessionsByNodeId.set(nodeId, existing);
      this.saveToSessionStorage();
      this.notify();
    }
  }

  public clearSession(key: string): void {
    if (!key) return;
    const norm = normalizeNodeAddress(key);

    const session = this.sessionsByAddress.get(norm) ?? this.sessionsByNodeId.get(key);
    this.sessionsByAddress.delete(norm);
    this.sessionsByNodeId.delete(key);

    if (session?.nodeId) {
      this.sessionsByNodeId.delete(session.nodeId);
    }
    const mapped = this.addressToNodeId.get(norm);
    if (mapped) {
      this.sessionsByNodeId.delete(mapped);
      this.addressToNodeId.delete(norm);
    }

    this.saveToSessionStorage();
    this.notify();
  }

  public clearAllSessions(): void {
    this.sessionsByAddress.clear();
    this.sessionsByNodeId.clear();
    this.addressToNodeId.clear();
    const storage = getSessionStorage();
    if (storage) {
      try {
        storage.removeItem(SESSION_STORAGE_KEY);
      } catch {}
    }
    this.notify();
  }
}

export const nodeAuthStore = new NodeAuthSessionStore();
