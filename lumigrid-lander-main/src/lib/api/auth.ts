import { apiJson, apiFetch } from './transport';

export type AuthRoleName = 'admin' | 'installer' | 'operator';

export interface AuthStatus {
  provisioned: boolean;
  bootstrapAllowed: boolean;
  factorySetupAllowed?: boolean;
}
export type AuthStatusResponse = AuthStatus;

export interface SessionResponse {
  authenticated: boolean;
  role: AuthRoleName;
  sessionToken: string;
  expiresIn: number;
  setupRequired?: boolean;
}

export interface SessionInfo {
  authenticated: boolean;
  role: AuthRoleName;
  expiresIn: number;
}

export interface WsTicketResponse {
  ticket: string;
  expiresInMs: number;
}

export interface CredentialsStatus {
  adminConfigured: boolean;
  installerConfigured: boolean;
  operatorConfigured: boolean;
}
export type CredentialsStatusResponse = CredentialsStatus;

export async function getAuthStatus(ip: string): Promise<AuthStatus> {
  return await apiJson<AuthStatus>(ip, '/api/v1/auth/status');
}

export async function bootstrapAuth(
  ip: string,
  adminToken: string,
  factorySetup = false
): Promise<SessionResponse & { provisioned: boolean }> {
  return await apiJson<SessionResponse & { provisioned: boolean }>(ip, '/api/v1/auth/bootstrap', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...(factorySetup ? { 'X-API-Key': 'lumigrid123' } : {}) },
    body: JSON.stringify({ adminToken }),
  });
}

export async function createSession(
  ip: string,
  token: string
): Promise<SessionResponse> {
  return await apiJson<SessionResponse>(ip, '/api/v1/auth/session', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ token }),
  });
}

export async function getSessionInfo(ip: string): Promise<SessionInfo> {
  return await apiJson<SessionInfo>(ip, '/api/v1/auth/session');
}

export async function deleteSession(ip: string): Promise<{ status: string }> {
  return await apiJson<{ status: string }>(ip, '/api/v1/auth/session', {
    method: 'DELETE',
  });
}

export async function createWsTicket(ip: string): Promise<WsTicketResponse> {
  return await apiJson<WsTicketResponse>(ip, '/api/v1/auth/ws-ticket', {
    method: 'POST',
  });
}

export async function getCredentialsStatus(ip: string): Promise<CredentialsStatus> {
  return await apiJson<CredentialsStatus>(ip, '/api/v1/auth/credentials');
}

export async function rotateCredential(
  ip: string,
  role: AuthRoleName,
  token: string
): Promise<{ status: string; role: string }> {
  return await apiJson<{ status: string; role: string }>(ip, '/api/v1/auth/credentials', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ role, token }),
  });
}

export async function disableCredential(
  ip: string,
  role: 'installer' | 'operator'
): Promise<{ status: string; role: string }> {
  return await apiJson<{ status: string; role: string }>(ip, '/api/v1/auth/credentials', {
    method: 'DELETE',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ role }),
  });
}
