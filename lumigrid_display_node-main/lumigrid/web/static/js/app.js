// Lumigrid Display Node - JavaScript

// API base URL
const API_BASE = '';

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    // Start status updates
    updateStatus();
    setInterval(updateStatus, 5000); // Update every 5 seconds
    
    // Handle Enter key in message input
    const messageInput = document.getElementById('message-input');
    if (messageInput) {
        messageInput.addEventListener('keypress', function(e) {
            if (e.key === 'Enter') {
                sendMessage();
            }
        });
    }
    
    // Setup sidebar toggle for mobile
    setupSidebarToggle();
    
    // Update active nav link
    updateActiveNavLink();
});

// Setup sidebar toggle
function setupSidebarToggle() {
    const toggle = document.getElementById('sidebar-toggle');
    const sidebar = document.getElementById('sidebar');
    
    if (toggle && sidebar) {
        toggle.addEventListener('click', function() {
            const isOpen = sidebar.classList.toggle('open');
            toggle.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
        });
        
        // Close sidebar when clicking outside on mobile
        document.addEventListener('click', function(e) {
            if (window.innerWidth <= 1024) {
                if (!sidebar.contains(e.target) && sidebar.classList.contains('open')) {
                    sidebar.classList.remove('open');
                    toggle.setAttribute('aria-expanded', 'false');
                }
            }
        });
    }
}

// Update active navigation link based on current path
function updateActiveNavLink() {
    const currentPath = window.location.pathname;
    document.querySelectorAll('.nav-link').forEach(link => {
        link.classList.remove('active');
        if (link.getAttribute('href') === currentPath) {
            link.classList.add('active');
        }
    });
}

// Update status display
async function updateStatus() {
    try {
        // Get full status
        const statusResponse = await fetch(`${API_BASE}/api/status`);
        if (statusResponse.ok) {
            const status = await statusResponse.json();
            
            // Update mode
            const modeEl = document.getElementById('current-mode');
            if (modeEl) {
                modeEl.textContent = capitalizeFirst(status.mode || 'Unknown');
            }
            
            // Update current frame if in frames mode
            const frameEl = document.getElementById('current-frame');
            if (frameEl) {
                if (status.current_frame) {
                    frameEl.textContent = status.current_frame.name || '-';
                } else {
                    frameEl.textContent = '-';
                }
            }
            
            // Update temperature
            const tempEl = document.getElementById('temperature');
            if (tempEl) {
                if (status.temperature !== null && status.temperature !== undefined) {
                    tempEl.textContent = `${status.temperature.toFixed(1)}°C`;
                } else {
                    tempEl.textContent = 'N/A';
                }
            }
            
            // Update humidity
            const humidityEl = document.getElementById('humidity');
            if (humidityEl) {
                if (status.humidity !== null && status.humidity !== undefined) {
                    humidityEl.textContent = `${status.humidity.toFixed(0)}%`;
                } else {
                    humidityEl.textContent = 'N/A';
                }
            }
            
            // Update brightness
            const brightnessEl = document.getElementById('brightness-value');
            const brightnessSlider = document.getElementById('brightness-slider');
            const brightnessSliderValue = document.getElementById('brightness-slider-value');
            
            if (brightnessEl) {
                brightnessEl.textContent = `${status.brightness}%`;
            }
            if (brightnessSlider) {
                brightnessSlider.value = status.brightness;
            }
            if (brightnessSliderValue) {
                brightnessSliderValue.textContent = `${status.brightness}%`;
            }
            
            // Update running status
            const runningEl = document.getElementById('running-status');
            if (runningEl) {
                runningEl.textContent = status.running ? 'Running' : 'Stopped';
                runningEl.className = `status-value status-badge ${status.running ? 'running' : 'stopped'}`;
            }
            
            // Update RTC time
            const rtcEl = document.getElementById('rtc-time');
            if (rtcEl && status.rtc_time) {
                const rtcDate = new Date(status.rtc_time);
                rtcEl.textContent = rtcDate.toLocaleTimeString();
            }
            
            // Update sidebar status indicator
            const systemStatus = document.getElementById('system-status');
            if (systemStatus) {
                const statusDot = systemStatus.querySelector('.status-dot');
                const statusText = systemStatus.querySelector('.status-text');
                if (statusDot && statusText) {
                    statusDot.style.background = status.running ? '#10b981' : '#ef4444';
                    statusText.textContent = status.running ? 'System Running' : 'System Stopped';
                }
            }
            
            // Update mode buttons active state
            updateModeButtons(status.mode);
            
            // Update control mode display
            const controlModeEl = document.getElementById('control-mode');
            if (controlModeEl && status.control_mode) {
                controlModeEl.textContent = capitalizeFirst(status.control_mode);
            }
        }
    } catch (error) {
        console.error('Error updating status:', error);
        
        // Update sidebar status to show error
        const systemStatus = document.getElementById('system-status');
        if (systemStatus) {
            const statusDot = systemStatus.querySelector('.status-dot');
            const statusText = systemStatus.querySelector('.status-text');
            if (statusDot && statusText) {
                statusDot.style.background = '#f59e0b';
                statusText.textContent = 'Connection Issue';
            }
        }
    }
}

// Update mode button active states
function updateModeButtons(currentMode) {
    document.querySelectorAll('.mode-btn[data-mode]').forEach(btn => {
        const isActive = btn.dataset.mode === currentMode;
        btn.classList.toggle('active', isActive);
        btn.setAttribute('aria-pressed', isActive ? 'true' : 'false');
    });
}

// Set display mode
async function setMode(mode) {
    try {
        const response = await fetch(`${API_BASE}/api/mode`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ mode: mode })
        });

        if (response.ok) {
            showNotification(`Mode set to ${capitalizeFirst(mode)}`, 'success');
            updateStatus();
        } else {
            const error = await response.json();
            showNotification(`Error: ${error.error}`, 'error');
        }
    } catch (error) {
        console.error('Error setting mode:', error);
        showNotification('Failed to set mode', 'error');
    }
}

// Send custom message
async function sendMessage() {
    const messageInput = document.getElementById('message-input');
    const message = messageInput.value.trim();

    if (!message) {
        showNotification('Please enter a message', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/api/message`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ message: message })
        });

        if (response.ok) {
            showNotification('Message sent successfully', 'success');
            messageInput.value = '';
            // Automatically switch to message mode
            await setMode('message');
        } else {
            const error = await response.json();
            showNotification(`Error: ${error.error}`, 'error');
        }
    } catch (error) {
        console.error('Error sending message:', error);
        showNotification('Failed to send message', 'error');
    }
}

// Update brightness label
function updateBrightnessLabel(value) {
    const brightnessSliderValue = document.getElementById('brightness-slider-value');
    if (brightnessSliderValue) {
        brightnessSliderValue.textContent = `${value}%`;
    }
}

// Set brightness
async function setBrightness(value) {
    try {
        const response = await fetch(`${API_BASE}/api/brightness`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ brightness: parseInt(value) })
        });

        if (response.ok) {
            showNotification(`Brightness set to ${value}%`, 'success');
            updateStatus();
        } else {
            const error = await response.json();
            showNotification(`Error: ${error.error}`, 'error');
        }
    } catch (error) {
        console.error('Error setting brightness:', error);
        showNotification('Failed to set brightness', 'error');
    }
}

// Show notification
function showNotification(message, type = 'success') {
    const notification = document.getElementById('notification');
    if (!notification) return;
    
    notification.textContent = message;
    notification.className = 'notification';
    
    if (type === 'error') {
        notification.classList.add('error');
    } else {
        notification.classList.add('success');
    }

    // Show notification
    setTimeout(() => {
        notification.classList.remove('hidden');
    }, 10);

    // Hide after 3 seconds
    setTimeout(() => {
        notification.classList.add('hidden');
    }, 3000);
}

// Helper function to capitalize first letter
function capitalizeFirst(str) {
    if (!str) return '';
    return str.charAt(0).toUpperCase() + str.slice(1);
}
