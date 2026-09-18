import { apiJson } from './transport';

export type ChannelKind = 'addressable' | 'pwm';
export type AddressableType = 'WS2812' | 'SK6812';
export type PixelOrder = 'RGB' | 'GRB' | 'RGBW' | 'GRBW';

export interface AddressableChannelInfo {
  type: AddressableType;
  pixels: number;
  pixelOrder: PixelOrder;
}

export interface HardwareChannel {
  index: number;
  port: string;
  kind: ChannelKind;
  hardwareId: number;
  gpio?: number;
  configured: boolean;
  enabled: boolean;
  name: string;
  maxBrightness: number;
  gammaCorrection: boolean;
  maxMilliwatts: number;
  dmxUniverse: number;
  dmxStartChannel: number;
  dmxMode: number;
  addressable?: AddressableChannelInfo;
}

export interface ChannelsResponse {
  channels: HardwareChannel[];
}

export interface ChannelLiveTelemetry {
  index: number;
  port: string;
  configured: boolean;
  enabled: boolean;
  owner: string;
  fault: boolean;
  brightness?: number;
  pixels?: number;
  level?: number;
}

export interface ChannelTestResult {
  accepted: boolean;
  commandId?: number;
  channel?: number;
  mode?: string;
  durationMs?: number;
}

export async function getChannels(ip: string): Promise<HardwareChannel[]> {
  const res = await apiJson<ChannelsResponse>(ip, '/api/v1/channels');
  return res.channels;
}

export function testChannel(
  ip: string,
  channel: number,
  mode = 'identify',
  durationMs = 3000,
): Promise<ChannelTestResult> {
  return apiJson<ChannelTestResult>(ip, '/api/v1/channel/test', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ channel, mode, durationMs }),
  });
}
