/**
 * API wrapper for LumiGrid REST API
 */

const API_BASE = '/api';

class APIError extends Error {
  constructor(message, status, data) {
    super(message);
    this.name = 'APIError';
    this.status = status;
    this.data = data;
  }
}

async function request(endpoint, options = {}) {
  const url = `${API_BASE}${endpoint}`;
  
  try {
    const response = await fetch(url, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        ...options.headers
      }
    });

    if (!response.ok) {
      const data = await response.json().catch(() => ({}));
      throw new APIError(
        data.message || `HTTP ${response.status}`,
        response.status,
        data
      );
    }

    return await response.json();
  } catch (error) {
    if (error instanceof APIError) {
      throw error;
    }
    throw new APIError('Network error', 0, { originalError: error.message });
  }
}

// System API
export const system = {
  async getStatus() {
    return request('/status');
  },
  
  async getState() {
    return request('/state');
  },
  
  async reboot() {
    // Use trigger endpoint with reboot action
    return request('/trigger', { 
      method: 'POST',
      body: JSON.stringify({ action: 'reboot' })
    });
  },
  
  async factoryReset() {
    return request('/config/restore-factory', { method: 'POST' });
  }
};

// Configuration API
export const config = {
  async get() {
    return request('/config');
  },
  
  async update(config) {
    return request('/config', {
      method: 'POST',
      body: JSON.stringify(config)
    });
  },
  
  async restoreFactory() {
    return request('/config/restore-factory', { method: 'POST' });
  }
};

// OTA API
export const ota = {
  async uploadFirmware(file, onProgress) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      
      if (onProgress) {
        xhr.upload.addEventListener('progress', (e) => {
          if (e.lengthComputable) {
            onProgress(Math.round((e.loaded / e.total) * 100));
          }
        });
      }
      
      xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
          resolve({ ok: true });
        } else {
          reject(new Error(`Upload failed: ${xhr.statusText}`));
        }
      });
      
      xhr.addEventListener('error', () => {
        reject(new Error('Network error during upload'));
      });
      
      xhr.open('POST', `${API_BASE}/ota/upload`);
      xhr.send(file);
    });
  },
  
  async uploadFromUrl(url) {
    const response = await fetch(`${API_BASE}/ota/url`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: url
    });
    
    if (!response.ok) {
      throw new Error(`OTA from URL failed: ${response.statusText}`);
    }
    
    return response.json();
  }
};

// Effects API
export const effects = {
  async list() {
    return request('/effects');
  },
  
  async apply(effectName, params) {
    return request('/effect/apply', {
      method: 'POST',
      body: JSON.stringify({
        effect: effectName,
        params
      })
    });
  },
  
  async applyToChannel(channel, effectName, params = {}) {
    // Use the /api/fx/apply endpoint for channel-specific effect application
    return request('/fx/apply', {
      method: 'POST',
      body: JSON.stringify({
        channel,
        name: effectName,
        params,
        brightness: params.brightness || 1.0
      })
    });
  },
  
  async stop() {
    return request('/trigger', { 
      method: 'POST',
      body: JSON.stringify({ action: 'blackout' })
    });
  }
};

// Sequencer API
export const sequencer = {
  async list() {
    return request('/sequences');
  },
  
  async get(id) {
    return request(`/sequences/${id}`);
  },
  
  async create(sequence) {
    return request('/sequences', {
      method: 'POST',
      body: JSON.stringify(sequence)
    });
  },
  
  async update(id, sequence) {
    return request(`/sequences/${id}`, {
      method: 'PUT',
      body: JSON.stringify(sequence)
    });
  },
  
  async delete(id) {
    return request(`/sequences/${id}`, {
      method: 'DELETE'
    });
  },
  
  async play(id) {
    return request(`/sequences/${id}/play`, { method: 'POST' });
  },
  
  async pause(id) {
    return request(`/sequences/${id}/pause`, { method: 'POST' });
  },
  
  async stop(id) {
    return request(`/sequences/${id}/stop`, { method: 'POST' });
  },
  
  async seek(id, timeMs) {
    return request(`/sequences/${id}/seek`, {
      method: 'POST',
      body: JSON.stringify({ time_ms: timeMs })
    });
  },
  
  async addTrack(id, track) {
    return request(`/sequences/${id}/tracks`, {
      method: 'POST',
      body: JSON.stringify(track)
    });
  },
  
  async addEvent(id, event) {
    return request(`/sequences/${id}/events`, {
      method: 'POST',
      body: JSON.stringify(event)
    });
  },
  
  async export(id) {
    return request(`/sequences/${id}/export`);
  },
  
  async import(data) {
    return request('/sequences/import', {
      method: 'POST',
      body: JSON.stringify(data)
    });
  }
};

// LED Preview API
export const leds = {
  async getPreview() {
    return request('/leds/preview');
  },
  
  async setPixel(channel, index, r, g, b) {
    return request('/leds/pixel', {
      method: 'POST',
      body: JSON.stringify({ channel, index, r, g, b })
    });
  }
};

// PWM API
export const pwm = {
  async setChannel(channel, duty) {
    return request(`/pwm/${channel}`, {
      method: 'POST',
      body: JSON.stringify({ duty })
    });
  },
  
  async startEffect(channel, effect, params) {
    return request(`/pwm/${channel}/effect`, {
      method: 'POST',
      body: JSON.stringify({ effect, params })
    });
  },
  
  async stopEffect(channel) {
    return request(`/pwm/${channel}/effect`, {
      method: 'DELETE'
    });
  }
};

// Presets API
export const presets = {
  async list() {
    return request('/presets');
  },
  
  async get(name) {
    return request(`/presets/${name}`);
  },
  
  async save(presetData) {
    // Matches the backend schema: preset_name, effect_id, speed, intensity, etc.
    return request('/presets', {
      method: 'POST',
      body: JSON.stringify(presetData)
    });
  },
  
  async delete(name) {
    return request(`/presets/${name}`, {
      method: 'DELETE'
    });
  }
};

// Trigger API
export const trigger = {
  async fire(action, options = {}) {
    return request('/trigger', {
      method: 'POST',
      body: JSON.stringify({ action, ...options })
    });
  },
  
  async flash(durationMs = 500) {
    return this.fire('flash', { duration_ms: durationMs });
  },
  
  async pulse(durationMs = 1000) {
    return this.fire('pulse', { duration_ms: durationMs });
  },
  
  async blackout() {
    return this.fire('blackout');
  },
  
  async applyPreset(preset, channel = 0, durationMs = 0) {
    return this.fire('apply_preset', { 
      preset, 
      channel, 
      duration_ms: durationMs 
    });
  }
};

// Sync API
export const sync = {
  async set(options) {
    return request('/sync/set', {
      method: 'POST',
      body: JSON.stringify(options)
    });
  },
  
  async pulse() {
    return request('/sync/pulse', { method: 'POST' });
  },
  
  async setMaster() {
    return request('/sync/master', { method: 'GET' });
  },
  
  async broadcastScene(effect, bpm) {
    return request('/sync/scene', {
      method: 'POST',
      body: JSON.stringify({ effect, bpm })
    });
  }
};

// Schedule API
export const schedule = {
  async get() {
    return request('/schedule');
  },
  
  async set(scheduleData) {
    return request('/schedule', {
      method: 'POST',
      body: JSON.stringify(scheduleData)
    });
  }
};

// Layout API
export const layout = {
  async get() {
    return request('/layout');
  },
  
  async set(layoutData) {
    return request('/layout', {
      method: 'POST',
      body: JSON.stringify(layoutData)
    });
  }
};

// Palette API
export const palettes = {
  async list() {
    return request('/palettes');
  },
  
  async setActive(name) {
    const response = await fetch(`${API_BASE}/palette`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: name
    });
    if (!response.ok) {
      throw new APIError('Failed to set palette', response.status);
    }
    return response.json();
  }
};

// SSE (Server-Sent Events) for real-time updates
export function createEventSource(endpoint) {
  const url = `${API_BASE}${endpoint}`;
  return new EventSource(url);
}

// Display API (for compatibility with display node)
export const display = {
  async getMode() {
    return request('/mode');
  },
  
  async setMode(mode) {
    return request('/mode', {
      method: 'POST',
      body: JSON.stringify({ mode })
    });
  },
  
  async getBrightness() {
    return request('/brightness');
  },
  
  async setBrightness(brightness) {
    return request('/brightness', {
      method: 'POST',
      body: JSON.stringify({ brightness })
    });
  },
  
  async setMessage(message) {
    return request('/message', {
      method: 'POST',
      body: JSON.stringify({ message })
    });
  }
};

// Frames API (for compatibility with display node)
export const frames = {
  async list() {
    return request('/frames');
  },
  
  async get(id) {
    return request(`/frames/${id}`);
  },
  
  async create(frame) {
    return request('/frames', {
      method: 'POST',
      body: JSON.stringify(frame)
    });
  },
  
  async update(id, frame) {
    return request(`/frames/${id}`, {
      method: 'PUT',
      body: JSON.stringify(frame)
    });
  },
  
  async delete(id) {
    return request(`/frames/${id}`, {
      method: 'DELETE'
    });
  },
  
  async reorder(order) {
    return request('/frames/reorder', {
      method: 'POST',
      body: JSON.stringify({ order })
    });
  },
  
  async toggle(id, enabled) {
    return request(`/frames/${id}/toggle`, {
      method: 'POST',
      body: JSON.stringify({ enabled })
    });
  }
};

// Font API (for compatibility with display node)
export const fonts = {
  async list() {
    return request('/fonts');
  },
  
  async upload(file) {
    const formData = new FormData();
    formData.append('font', file);
    
    const response = await fetch(`${API_BASE}/fonts/upload`, {
      method: 'POST',
      body: formData
    });
    
    if (!response.ok) {
      throw new APIError(`HTTP ${response.status}`, response.status);
    }
    
    return response.json();
  },
  
  async delete(path) {
    return request('/fonts/delete', {
      method: 'POST',
      body: JSON.stringify({ path })
    });
  }
};

// Time Settings API (for compatibility with display node)
export const time = {
  async getSettings() {
    return request('/time');
  },
  
  async updateSettings(settings) {
    return request('/time', {
      method: 'POST',
      body: JSON.stringify(settings)
    });
  }
};

// Control Settings API (for compatibility with display node)
export const control = {
  async getSettings() {
    return request('/control');
  },
  
  async updateSettings(settings) {
    return request('/control', {
      method: 'POST',
      body: JSON.stringify(settings)
    });
  }
};

export default {
  system,
  config,
  ota,
  effects,
  sequencer,
  leds,
  pwm,
  presets,
  trigger,
  sync,
  schedule,
  layout,
  palettes,
  display,
  frames,
  fonts,
  time,
  control,
  createEventSource
};
