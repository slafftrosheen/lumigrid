import { writable, derived } from 'svelte/store';

// System status
export const systemStatus = writable({
  connected: false,
  uptime: 0,
  heap_free: 0,
  wifi_rssi: 0,
  ip_address: '',
  hostname: 'lumigrid'
});

// Current effect
export const currentEffect = writable({
  name: 'None',
  params: {
    speed: 1.0,
    intensity: 0.5,
    brightness: 1.0,
    color: 0xFF0000
  }
});

// Performance metrics
export const metrics = writable({
  fps: 0,
  cpu_usage: 0,
  effect_render_time: 0
});

// Notifications/alerts
export const notifications = writable([]);

// Derived: Connection quality
export const connectionQuality = derived(
  systemStatus,
  $status => {
    if (!$status.connected) return 'disconnected';
    if ($status.wifi_rssi > -50) return 'excellent';
    if ($status.wifi_rssi > -70) return 'good';
    if ($status.wifi_rssi > -85) return 'fair';
    return 'poor';
  }
);

// Actions
export function updateStatus(status) {
  systemStatus.update(current => ({
    ...current,
    ...status,
    connected: true
  }));
}

export function setDisconnected() {
  systemStatus.update(current => ({
    ...current,
    connected: false
  }));
}

export function updateMetrics(newMetrics) {
  metrics.update(current => ({ ...current, ...newMetrics }));
}

export function addNotification(message, type = 'info') {
  const notification = {
    id: Date.now(),
    message,
    type, // 'info', 'success', 'warning', 'error'
    timestamp: Date.now()
  };
  
  notifications.update(list => [...list, notification]);
  
  // Auto-remove after 5 seconds
  setTimeout(() => {
    removeNotification(notification.id);
  }, 5000);
}

export function removeNotification(id) {
  notifications.update(list => list.filter(n => n.id !== id));
}
