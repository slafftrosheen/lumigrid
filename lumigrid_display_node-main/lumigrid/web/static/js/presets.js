// Lumigrid Preset Management
// Handles preset CRUD operations, import/export, and preview

let allPresets = [];
let filteredPresets = [];
let editingPresetName = null;
let presetToDelete = null;

// Load presets on page load
document.addEventListener('DOMContentLoaded', function() {
    loadPresets();
    setupColorSyncHandlers();
    updatePresetTypeFields();
    startPreviewUpdates();
});

// ==================== PRESET LOADING ====================

async function loadPresets() {
    try {
        const response = await fetch('/api/presets');
        if (response.ok) {
            const data = await response.json();
            allPresets = data.presets || [];
            filteredPresets = [...allPresets];
            renderPresets();
        } else {
            showNotification('Failed to load presets', 'error');
        }
    } catch (error) {
        console.error('Error loading presets:', error);
        showNotification('Failed to load presets', 'error');
    }
}

// ==================== PRESET RENDERING ====================

function renderPresets() {
    const container = document.getElementById('preset-grid');
    const countEl = document.getElementById('preset-count');
    
    countEl.textContent = `${filteredPresets.length} preset${filteredPresets.length !== 1 ? 's' : ''}`;
    
    if (filteredPresets.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <span class="empty-icon">📦</span>
                <p>No presets found.</p>
                <button class="btn btn-primary" onclick="showAddPresetModal()">Create Your First Preset</button>
            </div>
        `;
        return;
    }
    
    container.innerHTML = filteredPresets.map(preset => {
        const frameConfig = preset.frame_config || {};
        const type = frameConfig.type || 'unknown';
        const enabled = frameConfig.enabled !== false;
        const colors = frameConfig.colors || {};
        
        return `
            <div class="preset-card ${enabled ? '' : 'disabled'}" data-preset="${escapeHtml(preset.preset_name)}">
                <div class="preset-card-header">
                    <div class="preset-icon">${getPresetTypeIcon(type)}</div>
                    <div class="preset-meta">
                        <h3 class="preset-title">${escapeHtml(preset.preset_name)}</h3>
                        <p class="preset-description">${escapeHtml(preset.description || 'No description')}</p>
                    </div>
                </div>
                <div class="preset-card-body">
                    <div class="preset-details">
                        <div class="preset-detail-item">
                            <span class="detail-label">Type:</span>
                            <span class="detail-value">${escapeHtml(type)}</span>
                        </div>
                        <div class="preset-detail-item">
                            <span class="detail-label">Duration:</span>
                            <span class="detail-value">${frameConfig.duration || 10}s</span>
                        </div>
                        <div class="preset-detail-item">
                            <span class="detail-label">Status:</span>
                            <span class="detail-value status-badge ${enabled ? 'enabled' : 'disabled'}">
                                ${enabled ? 'Enabled' : 'Disabled'}
                            </span>
                        </div>
                    </div>
                    <div class="preset-preview-mini" style="background-color: ${colors.background || '#000'}; color: ${colors.text || '#FFF'}">
                        ${getPreviewText(frameConfig)}
                    </div>
                </div>
                <div class="preset-card-footer">
                    <button class="btn-icon" onclick="editPreset('${escapeHtml(preset.preset_name)}')" title="Edit">
                        ✏️
                    </button>
                    <button class="btn-icon" onclick="exportPreset('${escapeHtml(preset.preset_name)}')" title="Export">
                        📤
                    </button>
                    <button class="btn-icon btn-danger" onclick="showDeleteModal('${escapeHtml(preset.preset_name)}')" title="Delete">
                        🗑️
                    </button>
                </div>
            </div>
        `;
    }).join('');
}

function getPresetTypeIcon(type) {
    const icons = {
        'time': '🕐',
        'date': '📅',
        'datetime': '📆',
        'temperature': '🌡️',
        'humidity': '💧',
        'text': '💬',
        'message': '📢'
    };
    return icons[type] || '📄';
}

function getPreviewText(frameConfig) {
    const type = frameConfig.type;
    if (type === 'time') return '12:34:56';
    if (type === 'date') return '2026-01-09';
    if (type === 'datetime') return '2026-01-09 12:34';
    if (type === 'temperature') return '22°C';
    if (type === 'humidity') return '45%';
    if (type === 'text' || type === 'message') return frameConfig.text || 'Sample Text';
    return 'Preview';
}

function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// ==================== FILTERING & SORTING ====================

function filterPresets() {
    const searchTerm = document.getElementById('preset-search').value.toLowerCase();
    const typeFilter = document.getElementById('preset-type-filter').value;
    
    filteredPresets = allPresets.filter(preset => {
        const frameConfig = preset.frame_config || {};
        const matchesSearch = !searchTerm || 
            preset.preset_name.toLowerCase().includes(searchTerm) ||
            (preset.description && preset.description.toLowerCase().includes(searchTerm));
        const matchesType = !typeFilter || frameConfig.type === typeFilter;
        
        return matchesSearch && matchesType;
    });
    
    sortPresets();
}

function sortPresets() {
    const sortBy = document.getElementById('preset-sort').value;
    
    filteredPresets.sort((a, b) => {
        switch(sortBy) {
            case 'name':
                return a.preset_name.localeCompare(b.preset_name);
            case 'name-desc':
                return b.preset_name.localeCompare(a.preset_name);
            case 'type':
                const typeA = (a.frame_config || {}).type || '';
                const typeB = (b.frame_config || {}).type || '';
                return typeA.localeCompare(typeB);
            default:
                return 0;
        }
    });
    
    renderPresets();
}

// ==================== MODAL MANAGEMENT ====================

function showAddPresetModal() {
    editingPresetName = null;
    document.getElementById('modal-title').textContent = 'Add New Preset';
    document.getElementById('preset-form').reset();
    document.getElementById('frame-text-color').value = '#FFFFFF';
    document.getElementById('frame-text-color-hex').value = '#FFFFFF';
    document.getElementById('frame-bg-color').value = '#000000';
    document.getElementById('frame-bg-color-hex').value = '#000000';
    updatePresetTypeFields();
    updatePreview();
    document.getElementById('preset-modal').classList.add('show');
}

function editPreset(presetName) {
    const preset = allPresets.find(p => p.preset_name === presetName);
    if (!preset) return;
    
    editingPresetName = presetName;
    document.getElementById('modal-title').textContent = 'Edit Preset';
    
    const frameConfig = preset.frame_config || {};
    
    // Fill form with preset data
    document.getElementById('preset-id').value = presetName;
    document.getElementById('preset-name').value = preset.preset_name || '';
    document.getElementById('preset-description').value = preset.description || '';
    document.getElementById('frame-type').value = frameConfig.type || 'time';
    document.getElementById('frame-duration').value = frameConfig.duration || 10;
    document.getElementById('frame-enabled').checked = frameConfig.enabled !== false;
    document.getElementById('frame-format').value = frameConfig.format || '';
    document.getElementById('frame-text').value = frameConfig.text || '';
    document.getElementById('frame-unit').value = frameConfig.unit || 'celsius';
    
    // Font settings
    const fontSize = (frameConfig.font && frameConfig.font.size) || 14;
    document.getElementById('frame-font-size').value = fontSize;
    
    // Colors
    const textColor = (frameConfig.colors && frameConfig.colors.text) || '#FFFFFF';
    const bgColor = (frameConfig.colors && frameConfig.colors.background) || '#000000';
    document.getElementById('frame-text-color').value = textColor;
    document.getElementById('frame-text-color-hex').value = textColor;
    document.getElementById('frame-bg-color').value = bgColor;
    document.getElementById('frame-bg-color-hex').value = bgColor;
    
    // Position
    const posX = (frameConfig.position && frameConfig.position.x) || 'center';
    const posY = (frameConfig.position && frameConfig.position.y) || 'center';
    document.getElementById('frame-pos-x').value = posX;
    document.getElementById('frame-pos-y').value = posY;
    
    updatePresetTypeFields();
    updatePreview();
    document.getElementById('preset-modal').classList.add('show');
}

function closePresetModal() {
    document.getElementById('preset-modal').classList.remove('show');
    editingPresetName = null;
}

function showImportModal() {
    document.getElementById('import-modal').classList.add('show');
}

function closeImportModal() {
    document.getElementById('import-modal').classList.remove('show');
    document.getElementById('preset-json').value = '';
}

function showDeleteModal(presetName) {
    presetToDelete = presetName;
    document.getElementById('delete-preset-name').textContent = presetName;
    document.getElementById('delete-modal').classList.add('show');
}

function closeDeleteModal() {
    document.getElementById('delete-modal').classList.remove('show');
    presetToDelete = null;
}

// ==================== PRESET OPERATIONS ====================

async function savePreset() {
    const presetName = document.getElementById('preset-name').value.trim();
    const description = document.getElementById('preset-description').value.trim();
    
    if (!presetName) {
        showNotification('Preset name is required', 'error');
        return;
    }
    
    // Build preset data according to schema
    const presetData = {
        preset_name: presetName,
        description: description,
        frame_config: {
            type: document.getElementById('frame-type').value,
            // Note: frame_config.name is the display name shown in UI
            // We use description (user-friendly) or preset_name (technical) as fallback
            name: description || presetName,
            enabled: document.getElementById('frame-enabled').checked,
            duration: parseInt(document.getElementById('frame-duration').value),
            colors: {
                background: document.getElementById('frame-bg-color').value,
                text: document.getElementById('frame-text-color').value
            },
            font: {
                size: parseInt(document.getElementById('frame-font-size').value)
            },
            position: {
                x: document.getElementById('frame-pos-x').value,
                y: document.getElementById('frame-pos-y').value
            }
        }
    };
    
    // Add type-specific fields
    const type = presetData.frame_config.type;
    if (['time', 'date', 'datetime'].includes(type)) {
        const format = document.getElementById('frame-format').value;
        if (format) {
            presetData.frame_config.format = format;
        }
    }
    if (type === 'text' || type === 'message') {
        const text = document.getElementById('frame-text').value;
        if (text) {
            presetData.frame_config.text = text;
        }
    }
    if (type === 'temperature') {
        presetData.frame_config.unit = document.getElementById('frame-unit').value;
    }
    
    // Validate against schema
    const validationResult = validatePresetData(presetData);
    if (!validationResult.valid) {
        showNotification(`Validation error: ${validationResult.error}`, 'error');
        return;
    }
    
    try {
        const response = await fetch('/api/presets', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(presetData)
        });
        
        const responseData = await response.json();
        
        if (response.ok) {
            showNotification(editingPresetName ? 'Preset updated' : 'Preset created', 'success');
            closePresetModal();
            await loadPresets();
        } else {
            showNotification(responseData.error || 'Failed to save preset', 'error');
        }
    } catch (error) {
        console.error('Error saving preset:', error);
        showNotification('Failed to save preset', 'error');
    }
}

async function confirmDelete() {
    if (!presetToDelete) return;
    
    try {
        const response = await fetch(`/api/presets/${encodeURIComponent(presetToDelete)}`, {
            method: 'DELETE'
        });
        
        if (response.ok) {
            showNotification('Preset deleted', 'success');
            closeDeleteModal();
            await loadPresets();
        } else {
            const data = await response.json();
            showNotification(data.error || 'Failed to delete preset', 'error');
        }
    } catch (error) {
        console.error('Error deleting preset:', error);
        showNotification('Failed to delete preset', 'error');
    }
}

// ==================== IMPORT/EXPORT ====================

async function exportPreset(presetName) {
    const preset = allPresets.find(p => p.preset_name === presetName);
    if (!preset) return;
    
    const json = JSON.stringify(preset, null, 2);
    downloadJSON(json, `${presetName}.json`);
    showNotification('Preset exported', 'success');
}

function exportCurrentPreset() {
    // Export the currently editing preset
    const presetName = document.getElementById('preset-name').value.trim() || 'preset';
    const description = document.getElementById('preset-description').value.trim();
    
    const presetData = {
        preset_name: presetName,
        description: description,
        frame_config: {
            type: document.getElementById('frame-type').value,
            name: description || presetName, // Use description for display name, fallback to preset_name
            enabled: document.getElementById('frame-enabled').checked,
            duration: parseInt(document.getElementById('frame-duration').value),
            colors: {
                background: document.getElementById('frame-bg-color').value,
                text: document.getElementById('frame-text-color').value
            },
            font: {
                size: parseInt(document.getElementById('frame-font-size').value)
            },
            position: {
                x: document.getElementById('frame-pos-x').value,
                y: document.getElementById('frame-pos-y').value
            }
        }
    };
    
    // Add type-specific fields
    const type = presetData.frame_config.type;
    if (['time', 'date', 'datetime'].includes(type)) {
        const format = document.getElementById('frame-format').value;
        if (format) presetData.frame_config.format = format;
    }
    if (type === 'text' || type === 'message') {
        const text = document.getElementById('frame-text').value;
        if (text) presetData.frame_config.text = text;
    }
    if (type === 'temperature') {
        presetData.frame_config.unit = document.getElementById('frame-unit').value;
    }
    
    const json = JSON.stringify(presetData, null, 2);
    downloadJSON(json, `${presetName}.json`);
    showNotification('Preset exported', 'success');
}

function downloadJSON(content, filename) {
    const blob = new Blob([content], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

async function handleFileImport(event) {
    const file = event.target.files[0];
    if (!file) return;
    
    try {
        const text = await file.text();
        const preset = JSON.parse(text);
        await importPreset(preset);
        event.target.value = ''; // Reset file input
    } catch (error) {
        console.error('Error importing file:', error);
        showNotification('Invalid JSON file', 'error');
    }
}

async function importFromText() {
    const text = document.getElementById('preset-json').value.trim();
    if (!text) {
        showNotification('Please paste JSON content', 'error');
        return;
    }
    
    try {
        const preset = JSON.parse(text);
        await importPreset(preset);
    } catch (error) {
        console.error('Error parsing JSON:', error);
        showNotification('Invalid JSON format', 'error');
    }
}

async function importPreset(presetData) {
    // Validate against schema
    const validationResult = validatePresetData(presetData);
    if (!validationResult.valid) {
        showNotification(`Validation error: ${validationResult.error}`, 'error');
        return;
    }
    
    try {
        const response = await fetch('/api/presets', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(presetData)
        });
        
        const responseData = await response.json();
        
        if (response.ok) {
            showNotification('Preset imported successfully', 'success');
            closeImportModal();
            await loadPresets();
        } else {
            showNotification(responseData.error || 'Failed to import preset', 'error');
        }
    } catch (error) {
        console.error('Error importing preset:', error);
        showNotification('Failed to import preset', 'error');
    }
}

// ==================== VALIDATION ====================

// Valid frame types from preset-display.schema.json
// NOTE: This must be kept in sync with the schema's frame_config.type enum
// If the schema is updated, this constant must be updated as well
const VALID_FRAME_TYPES = [
    'time', 'date', 'datetime', 'temperature', 'humidity', 'text', 'message',
    'data_visualization', 'interactive', 'template', 'conditional', 
    'multi_page', 'scrolling', 'animated'
];

function validatePresetData(data) {
    // Basic client-side validation based on preset-display.schema.json
    
    // Required fields
    if (!data.preset_name || typeof data.preset_name !== 'string') {
        return { valid: false, error: 'preset_name is required and must be a string' };
    }
    
    if (data.preset_name.length === 0 || data.preset_name.length > 100) {
        return { valid: false, error: 'preset_name must be 1-100 characters' };
    }
    
    if (data.description && typeof data.description !== 'string') {
        return { valid: false, error: 'description must be a string' };
    }
    
    if (data.description && data.description.length > 500) {
        return { valid: false, error: 'description must be max 500 characters' };
    }
    
    // frame_config validation
    if (!data.frame_config || typeof data.frame_config !== 'object') {
        return { valid: false, error: 'frame_config is required and must be an object' };
    }
    
    if (!data.frame_config.type || !VALID_FRAME_TYPES.includes(data.frame_config.type)) {
        return { valid: false, error: `frame_config.type must be one of: ${VALID_FRAME_TYPES.join(', ')}` };
    }
    
    // Duration validation
    if (data.frame_config.duration !== undefined) {
        const duration = data.frame_config.duration;
        if (!Number.isInteger(duration) || duration < 1 || duration > 3600) {
            return { valid: false, error: 'duration must be an integer between 1 and 3600' };
        }
    }
    
    // Color validation
    if (data.frame_config.colors) {
        const colorPattern = /^#?([0-9A-Fa-f]{3}|[0-9A-Fa-f]{6})$/;
        if (data.frame_config.colors.background && !colorPattern.test(data.frame_config.colors.background)) {
            return { valid: false, error: 'colors.background must be a valid hex color' };
        }
        if (data.frame_config.colors.text && !colorPattern.test(data.frame_config.colors.text)) {
            return { valid: false, error: 'colors.text must be a valid hex color' };
        }
    }
    
    // Font size validation
    if (data.frame_config.font && data.frame_config.font.size !== undefined) {
        const size = data.frame_config.font.size;
        if (!Number.isInteger(size) || size < 6 || size > 72) {
            return { valid: false, error: 'font.size must be an integer between 6 and 72' };
        }
    }
    
    // Unit validation
    if (data.frame_config.unit && !['celsius', 'fahrenheit'].includes(data.frame_config.unit)) {
        return { valid: false, error: 'unit must be either celsius or fahrenheit' };
    }
    
    return { valid: true };
}

// ==================== UI HELPERS ====================

function updatePresetTypeFields() {
    const type = document.getElementById('frame-type').value;
    
    document.getElementById('format-group').style.display = 
        ['time', 'date', 'datetime'].includes(type) ? 'block' : 'none';
    document.getElementById('text-group').style.display = 
        ['text', 'message'].includes(type) ? 'block' : 'none';
    document.getElementById('unit-group').style.display = 
        type === 'temperature' ? 'block' : 'none';
    
    // Set default format based on type
    if (!editingPresetName) {
        const formats = {
            'time': '%H:%M:%S',
            'date': '%Y-%m-%d',
            'datetime': '%Y-%m-%d %H:%M'
        };
        if (formats[type]) {
            document.getElementById('frame-format').value = formats[type];
        }
    }
    
    updatePreview();
}

function syncColorInput(colorInputId, hexValue) {
    if (/^#[0-9A-Fa-f]{6}$/.test(hexValue)) {
        document.getElementById(colorInputId).value = hexValue;
        updatePreview();
    }
}

function setupColorSyncHandlers() {
    const inputs = [
        { color: 'frame-text-color', hex: 'frame-text-color-hex' },
        { color: 'frame-bg-color', hex: 'frame-bg-color-hex' }
    ];
    
    inputs.forEach(({ color, hex }) => {
        const colorInput = document.getElementById(color);
        const hexInput = document.getElementById(hex);
        
        if (colorInput && hexInput) {
            colorInput.addEventListener('input', function() {
                hexInput.value = this.value;
                updatePreview();
            });
        }
    });
    
    // Add listeners to all form fields to update preview
    const formFields = ['frame-type', 'frame-text', 'frame-format', 'frame-unit', 'frame-font-size'];
    formFields.forEach(fieldId => {
        const field = document.getElementById(fieldId);
        if (field) {
            field.addEventListener('input', updatePreview);
            field.addEventListener('change', updatePreview);
        }
    });
}

// ==================== PREVIEW ====================

function updatePreview() {
    const previewFrame = document.getElementById('preview-frame');
    const previewText = document.getElementById('preview-text');
    
    if (!previewFrame || !previewText) return;
    
    const bgColor = document.getElementById('frame-bg-color').value;
    const textColor = document.getElementById('frame-text-color').value;
    const fontSize = document.getElementById('frame-font-size').value;
    const type = document.getElementById('frame-type').value;
    
    previewFrame.style.backgroundColor = bgColor;
    previewFrame.style.color = textColor;
    previewText.style.fontSize = `${fontSize}px`;
    
    // Update preview text based on type
    let displayText = 'Preview';
    if (type === 'time') {
        displayText = new Date().toLocaleTimeString();
    } else if (type === 'date') {
        displayText = new Date().toLocaleDateString();
    } else if (type === 'datetime') {
        displayText = new Date().toLocaleString();
    } else if (type === 'temperature') {
        displayText = '22°C';
    } else if (type === 'humidity') {
        displayText = '45%';
    } else if (type === 'text' || type === 'message') {
        const text = document.getElementById('frame-text').value;
        displayText = text || 'Sample Text';
    }
    
    previewText.textContent = displayText;
}

function startPreviewUpdates() {
    // Update preview every second for time-based displays
    setInterval(() => {
        const type = document.getElementById('frame-type')?.value;
        if (['time', 'datetime'].includes(type)) {
            updatePreview();
        }
    }, 1000);
}
