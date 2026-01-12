/**
 * Timeline Editor - Manages timeline-based preset sequencing
 * Enhanced visual drag-and-drop timeline editor
 */

// ==================== STATE ====================
let timelines = [];
let presets = [];
let currentEditingTimelineId = null;
let selectedTimelineId = null;
let selectedBlockIndex = null;

// Visual editor state
let zoomLevel = 100;
const ZOOM_STEP = 25;
const MIN_ZOOM = 50;
const MAX_ZOOM = 200;
const PIXELS_PER_MINUTE = 2; // At 100% zoom
const TRACK_HEIGHT = 60;

// Drag state
let isDragging = false;
let isResizing = false;
let dragStartX = 0;
let dragStartBlockLeft = 0;
let dragStartBlockWidth = 0;
let resizeDirection = null;
let currentDragBlock = null;

// ==================== INITIALIZATION ====================
document.addEventListener('DOMContentLoaded', function() {
    initializeTimelines();
    setupEventListeners();
    setupVisualEditorListeners();
    setupPlaybackListeners();
    setupCalendarListeners();
    setupChannelGroupsListeners();
    setupPresetLinksListeners();
    setupConditionalRulesListeners();
});

function setupEventListeners() {
    // Create timeline button
    document.getElementById('create-timeline-btn').addEventListener('click', () => {
        openTimelineModal();
    });
    
    // Save timeline button
    document.getElementById('save-timeline-btn').addEventListener('click', () => {
        saveTimeline();
    });
    
    // Add entry button
    document.getElementById('add-entry-btn').addEventListener('click', () => {
        addTimelineEntry();
    });
    
    // Modal close buttons
    document.querySelectorAll('.modal-close').forEach(btn => {
        btn.addEventListener('click', (e) => {
            const modal = e.target.closest('.modal');
            if (modal) {
                modal.classList.add('hidden');
            }
        });
    });
    
    // Close modal on background click
    document.querySelectorAll('.modal').forEach(modal => {
        modal.addEventListener('click', (e) => {
            if (e.target === modal) {
                modal.classList.add('hidden');
            }
        });
    });
}

function setupVisualEditorListeners() {
    // Timeline selector
    const timelineSelect = document.getElementById('timeline-select');
    if (timelineSelect) {
        timelineSelect.addEventListener('change', (e) => {
            selectTimeline(e.target.value);
        });
    }
    
    // Zoom controls
    const zoomInBtn = document.getElementById('zoom-in-btn');
    const zoomOutBtn = document.getElementById('zoom-out-btn');
    if (zoomInBtn) {
        zoomInBtn.addEventListener('click', () => {
            setZoom(zoomLevel + ZOOM_STEP);
        });
    }
    if (zoomOutBtn) {
        zoomOutBtn.addEventListener('click', () => {
            setZoom(zoomLevel - ZOOM_STEP);
        });
    }
    
    // Add track button
    const addTrackBtn = document.getElementById('add-track-btn');
    if (addTrackBtn) {
        addTrackBtn.addEventListener('click', addTrack);
    }
    
    // Add block button
    const addBlockBtn = document.getElementById('add-block-btn');
    if (addBlockBtn) {
        addBlockBtn.addEventListener('click', addBlock);
    }
    
    // Block properties
    const applyPropsBtn = document.getElementById('apply-block-props-btn');
    if (applyPropsBtn) {
        applyPropsBtn.addEventListener('click', applyBlockProperties);
    }
    
    const deleteBlockBtn = document.getElementById('delete-block-btn');
    if (deleteBlockBtn) {
        deleteBlockBtn.addEventListener('click', deleteSelectedBlock);
    }
    
    // Keyboard navigation
    const tracksContent = document.getElementById('tracks-content');
    if (tracksContent) {
        tracksContent.addEventListener('keydown', handleKeyboardNavigation);
    }
    
    // Global mouse events for dragging
    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('mouseup', handleMouseUp);
}

function setupPlaybackListeners() {
    // Play button
    const playBtn = document.getElementById('play-btn');
    if (playBtn) {
        playBtn.addEventListener('click', startPlayback);
    }
    
    // Stop button
    const stopBtn = document.getElementById('stop-btn');
    if (stopBtn) {
        stopBtn.addEventListener('click', stopPlayback);
    }
    
    // Rewind button
    const rewindBtn = document.getElementById('rewind-btn');
    if (rewindBtn) {
        rewindBtn.addEventListener('click', rewindPlayback);
    }
    
    // Playback mode buttons
    document.querySelectorAll('.playback-mode-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            document.querySelectorAll('.playback-mode-btn').forEach(b => {
                b.classList.remove('active');
                b.setAttribute('aria-pressed', 'false');
            });
            e.currentTarget.classList.add('active');
            e.currentTarget.setAttribute('aria-pressed', 'true');
        });
    });
}

function setupCalendarListeners() {
    // Add exception date
    const addExceptionBtn = document.getElementById('add-exception-btn');
    if (addExceptionBtn) {
        addExceptionBtn.addEventListener('click', addExceptionDate);
    }
    
    // Apply calendar settings
    const applyCalendarBtn = document.getElementById('apply-calendar-btn');
    if (applyCalendarBtn) {
        applyCalendarBtn.addEventListener('click', applyCalendarSettings);
    }
}

// ==================== DATA LOADING ====================
async function initializeTimelines() {
    await loadPresets();
    await loadTimelines();
    await loadSequencerStatus();
    
    // Initialize visual editor
    renderTimeRuler();
    updateTimelineSelector();
    
    // Refresh status every 5 seconds
    setInterval(loadSequencerStatus, 5000);
}

async function loadPresets() {
    try {
        const response = await fetch('/api/presets');
        const data = await response.json();
        presets = data.presets || [];
        populatePresetDropdowns();
    } catch (error) {
        console.error('Error loading presets:', error);
        showNotification('Error loading presets', 'error');
    }
}

async function loadTimelines() {
    try {
        const response = await fetch('/api/timelines');
        const data = await response.json();
        timelines = data.timelines || [];
        renderTimelines();
        updateTimelineSelector();
        updateStats();
    } catch (error) {
        console.error('Error loading timelines:', error);
        showNotification('Error loading timelines', 'error');
        document.getElementById('timelines-list').innerHTML = '<div class="empty-state">Error loading timelines</div>';
    }
}

async function loadSequencerStatus() {
    try {
        const response = await fetch('/api/sequencer/status');
        const status = await response.json();
        updateSequencerStatus(status);
    } catch (error) {
        console.error('Error loading sequencer status:', error);
    }
}

function updateSequencerStatus(status) {
    const statusEl = document.getElementById('sequencer-status');
    if (statusEl) {
        statusEl.textContent = status.running ? 'Running' : 'Stopped';
        statusEl.className = 'status-value status-badge ' + (status.running ? 'running' : 'stopped');
    }
    
    const activeTimeline = document.getElementById('active-timeline');
    if (activeTimeline) {
        activeTimeline.textContent = status.active_timeline || '-';
    }
    
    const activePreset = document.getElementById('active-preset');
    if (activePreset) {
        activePreset.textContent = status.active_entry || '-';
    }
    
    updateStats();
}

function updateStats() {
    const enabledCount = timelines.filter(t => t.enabled).length;
    const enabledCountEl = document.getElementById('enabled-count');
    const totalTimelinesEl = document.getElementById('total-timelines');
    
    if (enabledCountEl) {
        enabledCountEl.textContent = enabledCount;
    }
    if (totalTimelinesEl) {
        totalTimelinesEl.textContent = timelines.length;
    }
}

function populatePresetDropdowns() {
    const options = '<option value="">Select preset...</option>' +
        presets.map(p => `<option value="${p.preset_name}">${escapeHtml(p.description || p.preset_name)}</option>`).join('');
    
    // Block properties preset dropdown
    const blockPreset = document.getElementById('block-preset');
    if (blockPreset) {
        blockPreset.innerHTML = options;
    }
}

function renderTimelines() {
    const container = document.getElementById('timelines-list');
    
    if (timelines.length === 0) {
        container.innerHTML = '<div class="empty-state">No timelines created yet. Click "Create Timeline" to get started.</div>';
        return;
    }
    
    container.innerHTML = timelines.map(timeline => `
        <div class="timeline-item">
            <div class="timeline-header">
                <div class="timeline-title">${escapeHtml(timeline.name)}</div>
                <div class="timeline-actions">
                    <button class="btn btn-small btn-secondary" onclick="editTimeline('${timeline.id}')">Edit</button>
                    <button class="btn btn-small ${timeline.enabled ? 'btn-warning' : 'btn-success'}" 
                            onclick="toggleTimeline('${timeline.id}', ${!timeline.enabled})">
                        ${timeline.enabled ? 'Deactivate' : 'Activate'}
                    </button>
                    <button class="btn btn-small btn-danger" onclick="deleteTimeline('${timeline.id}')">Delete</button>
                </div>
            </div>
            <div class="timeline-info">
                ${timeline.description ? `<div>${escapeHtml(timeline.description)}</div>` : ''}
                <div class="timeline-entries-count">
                    ${timeline.entries.length} entries | 
                    ${timeline.enabled ? '✅ Enabled' : '⏸️ Disabled'} | 
                    ${timeline.loop ? '🔄 Loop' : '▶️ Once'}
                </div>
            </div>
        </div>
    `).join('');
}

function openTimelineModal(timeline = null) {
    currentEditingTimelineId = timeline ? timeline.id : null;
    
    // Set form values
    document.getElementById('timeline-id').value = timeline ? timeline.id : '';
    document.getElementById('timeline-name').value = timeline ? timeline.name : '';
    document.getElementById('timeline-description').value = timeline ? timeline.description : '';
    document.getElementById('timeline-enabled').checked = timeline ? timeline.enabled : false;
    document.getElementById('timeline-loop').checked = timeline ? timeline.loop : false;
    
    // Clear and populate entries
    const entriesContainer = document.getElementById('timeline-entries');
    entriesContainer.innerHTML = '';
    
    if (timeline && timeline.entries) {
        timeline.entries.forEach(entry => {
            addTimelineEntry(entry);
        });
    } else {
        // Add one empty entry by default
        addTimelineEntry();
    }
    
    // Update modal title
    document.getElementById('timeline-modal-title').textContent = timeline ? 'Edit Timeline' : 'Create Timeline';
    
    // Show modal
    document.getElementById('timeline-modal').classList.remove('hidden');
}

function closeTimelineModal() {
    document.getElementById('timeline-modal').classList.add('hidden');
    currentEditingTimelineId = null;
}

function addTimelineEntry(entry = null) {
    const template = document.getElementById('entry-template');
    const clone = template.content.cloneNode(true);
    const container = document.getElementById('timeline-entries');
    
    // Set entry number
    const entryNumber = container.children.length + 1;
    clone.querySelector('.entry-number').textContent = entryNumber;
    
    // Populate preset dropdown
    const presetSelect = clone.querySelector('.entry-preset');
    presetSelect.innerHTML = '<option value="">Select a preset</option>' +
        presets.map(p => `<option value="${p.preset_name}">${escapeHtml(p.description)}</option>`).join('');
    
    // Set values if editing
    if (entry) {
        presetSelect.value = entry.preset_name || '';
        clone.querySelector('.entry-trigger-type').value = entry.trigger_type || 'time_of_day';
        clone.querySelector('.entry-time').value = entry.time_of_day ? entry.time_of_day.substring(0, 8) : '';
        clone.querySelector('.entry-datetime').value = entry.datetime || '';
        clone.querySelector('.entry-interval').value = entry.interval_seconds || '';
        clone.querySelector('.entry-duration').value = entry.duration || 0;
        clone.querySelector('.entry-enabled').checked = entry.enabled !== false;
        
        // Set days of week
        if (entry.days_of_week) {
            clone.querySelectorAll('.day-checkbox').forEach(cb => {
                cb.checked = entry.days_of_week.includes(parseInt(cb.value));
            });
        }
    }
    
    // Setup trigger type change handler
    const entryElement = clone.querySelector('.timeline-entry');
    container.appendChild(clone);
    
    const triggerTypeSelect = entryElement.querySelector('.entry-trigger-type');
    const updateTriggerFields = () => {
        const triggerType = triggerTypeSelect.value;
        entryElement.querySelectorAll('.trigger-time-of-day, .trigger-datetime, .trigger-interval').forEach(el => {
            el.classList.add('hidden');
        });
        
        if (triggerType === 'time_of_day') {
            entryElement.querySelectorAll('.trigger-time-of-day').forEach(el => el.classList.remove('hidden'));
        } else if (triggerType === 'datetime') {
            entryElement.querySelector('.trigger-datetime').classList.remove('hidden');
        } else if (triggerType === 'interval') {
            entryElement.querySelector('.trigger-interval').classList.remove('hidden');
        }
    };
    
    triggerTypeSelect.addEventListener('change', updateTriggerFields);
    updateTriggerFields();
    
    // Setup remove button
    entryElement.querySelector('.remove-entry-btn').addEventListener('click', () => {
        entryElement.remove();
        // Renumber entries
        document.querySelectorAll('.timeline-entry').forEach((entry, index) => {
            entry.querySelector('.entry-number').textContent = index + 1;
        });
    });
}

async function saveTimeline() {
    const timelineData = {
        name: document.getElementById('timeline-name').value,
        description: document.getElementById('timeline-description').value,
        enabled: document.getElementById('timeline-enabled').checked,
        loop: document.getElementById('timeline-loop').checked,
        entries: []
    };
    
    // Validate
    if (!timelineData.name) {
        showNotification('Please enter a timeline name', 'error');
        return;
    }
    
    // Collect entries
    document.querySelectorAll('.timeline-entry').forEach(entryElement => {
        const triggerType = entryElement.querySelector('.entry-trigger-type').value;
        const entry = {
            preset_name: entryElement.querySelector('.entry-preset').value,
            trigger_type: triggerType,
            duration: parseInt(entryElement.querySelector('.entry-duration').value) || 0,
            enabled: entryElement.querySelector('.entry-enabled').checked
        };
        
        // Add trigger-specific fields
        if (triggerType === 'time_of_day') {
            const timeValue = entryElement.querySelector('.entry-time').value;
            // Ensure time has seconds (HH:MM:SS format)
            entry.time_of_day = timeValue.split(':').length === 2 ? timeValue + ':00' : timeValue;
            entry.days_of_week = Array.from(entryElement.querySelectorAll('.day-checkbox:checked'))
                .map(cb => parseInt(cb.value));
        } else if (triggerType === 'datetime') {
            entry.datetime = entryElement.querySelector('.entry-datetime').value;
        } else if (triggerType === 'interval') {
            entry.interval_seconds = parseInt(entryElement.querySelector('.entry-interval').value) || 0;
        }
        
        timelineData.entries.push(entry);
    });
    
    try {
        let response;
        if (currentEditingTimelineId) {
            // Update existing timeline
            response = await fetch(`/api/timelines/${currentEditingTimelineId}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(timelineData)
            });
        } else {
            // Create new timeline
            response = await fetch('/api/timelines', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(timelineData)
            });
        }
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification(currentEditingTimelineId ? 'Timeline updated' : 'Timeline created', 'success');
            closeTimelineModal();
            await loadTimelines();
        } else {
            showNotification(result.error || 'Error saving timeline', 'error');
        }
    } catch (error) {
        console.error('Error saving timeline:', error);
        showNotification('Error saving timeline', 'error');
    }
}

async function editTimeline(timelineId) {
    const timeline = timelines.find(t => t.id === timelineId);
    if (timeline) {
        openTimelineModal(timeline);
    }
}

async function toggleTimeline(timelineId, enable) {
    try {
        const endpoint = enable ? 'activate' : 'deactivate';
        const response = await fetch(`/api/timelines/${timelineId}/${endpoint}`, {
            method: 'POST'
        });
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification(`Timeline ${enable ? 'activated' : 'deactivated'}`, 'success');
            await loadTimelines();
        } else {
            showNotification(result.error || 'Error toggling timeline', 'error');
        }
    } catch (error) {
        console.error('Error toggling timeline:', error);
        showNotification('Error toggling timeline', 'error');
    }
}

async function deleteTimeline(timelineId) {
    if (!confirm('Are you sure you want to delete this timeline?')) {
        return;
    }
    
    try {
        const response = await fetch(`/api/timelines/${timelineId}`, {
            method: 'DELETE'
        });
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification('Timeline deleted', 'success');
            await loadTimelines();
        } else {
            showNotification(result.error || 'Error deleting timeline', 'error');
        }
    } catch (error) {
        console.error('Error deleting timeline:', error);
        showNotification('Error deleting timeline', 'error');
    }
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function showNotification(message, type = 'info') {
    const notification = document.getElementById('notification');
    notification.textContent = message;
    notification.className = `notification ${type}`;
    notification.classList.remove('hidden');
    
    setTimeout(() => {
        notification.classList.add('hidden');
    }, 3000);
}

// ==================== CHANNEL GROUPS ====================

let channelGroups = [];
let currentEditingGroupId = null;

function setupChannelGroupsListeners() {
    document.getElementById('manage-channel-groups-btn').addEventListener('click', () => {
        openChannelGroupsModal();
    });
    
    document.getElementById('add-channel-group-btn').addEventListener('click', () => {
        openChannelGroupEditor();
    });
    
    document.getElementById('save-channel-group-btn').addEventListener('click', () => {
        saveChannelGroup();
    });
    
    // Close modal handlers
    document.getElementById('channel-groups-modal').querySelectorAll('.modal-close').forEach(btn => {
        btn.addEventListener('click', () => {
            document.getElementById('channel-groups-modal').classList.add('hidden');
        });
    });
    
    document.getElementById('channel-group-editor-modal').querySelectorAll('.modal-close').forEach(btn => {
        btn.addEventListener('click', () => {
            document.getElementById('channel-group-editor-modal').classList.add('hidden');
        });
    });
}

async function openChannelGroupsModal() {
    await loadChannelGroups();
    document.getElementById('channel-groups-modal').classList.remove('hidden');
}

async function loadChannelGroups() {
    try {
        const response = await fetch('/api/channel-groups');
        const data = await response.json();
        channelGroups = data.channel_groups || [];
        renderChannelGroups();
    } catch (error) {
        console.error('Error loading channel groups:', error);
        showNotification('Error loading channel groups', 'error');
    }
}

function renderChannelGroups() {
    const container = document.getElementById('channel-groups-list');
    
    if (channelGroups.length === 0) {
        container.innerHTML = '<div class="empty-state">No channel groups created yet.</div>';
        return;
    }
    
    container.innerHTML = channelGroups.map(group => `
        <div class="item-card">
            <div class="item-info">
                <h4>${escapeHtml(group.name)}</h4>
                <p>${escapeHtml(group.description || '')}</p>
                <p><small>Channels: ${group.channels.join(', ')}</small></p>
            </div>
            <div class="item-actions">
                <button class="btn btn-small btn-secondary" onclick="editChannelGroup('${group.id}')">Edit</button>
                <button class="btn btn-small btn-danger" onclick="deleteChannelGroup('${group.id}')">Delete</button>
            </div>
        </div>
    `).join('');
}

function openChannelGroupEditor(group = null) {
    currentEditingGroupId = group ? group.id : null;
    
    document.getElementById('group-id').value = group ? group.id : '';
    document.getElementById('group-name').value = group ? group.name : '';
    document.getElementById('group-description').value = group ? (group.description || '') : '';
    document.getElementById('group-channels').value = group ? group.channels.join(', ') : '';
    document.getElementById('group-color').value = group ? (group.color || '#0071e3') : '#0071e3';
    document.getElementById('group-enabled').checked = group ? group.enabled !== false : true;
    
    document.getElementById('channel-group-editor-title').textContent = group ? 'Edit Channel Group' : 'Add Channel Group';
    document.getElementById('channel-group-editor-modal').classList.remove('hidden');
}

async function editChannelGroup(groupId) {
    const group = channelGroups.find(g => g.id === groupId);
    if (group) {
        openChannelGroupEditor(group);
    }
}

async function saveChannelGroup() {
    // Generate a more robust ID using timestamp + random component
    const generateId = () => `group-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
    
    const groupData = {
        id: document.getElementById('group-id').value || generateId(),
        name: document.getElementById('group-name').value,
        description: document.getElementById('group-description').value,
        channels: document.getElementById('group-channels').value.split(',').map(c => c.trim()).filter(c => c),
        color: document.getElementById('group-color').value,
        enabled: document.getElementById('group-enabled').checked
    };
    
    if (!groupData.name || groupData.channels.length === 0) {
        showNotification('Please fill in required fields', 'error');
        return;
    }
    
    try {
        let response;
        if (currentEditingGroupId) {
            response = await fetch(`/api/channel-groups/${currentEditingGroupId}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(groupData)
            });
        } else {
            response = await fetch('/api/channel-groups', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(groupData)
            });
        }
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification(currentEditingGroupId ? 'Channel group updated' : 'Channel group created', 'success');
            document.getElementById('channel-group-editor-modal').classList.add('hidden');
            await loadChannelGroups();
        } else {
            showNotification(result.error || 'Error saving channel group', 'error');
        }
    } catch (error) {
        console.error('Error saving channel group:', error);
        showNotification('Error saving channel group', 'error');
    }
}

async function deleteChannelGroup(groupId) {
    if (!confirm('Are you sure you want to delete this channel group?')) {
        return;
    }
    
    try {
        const response = await fetch(`/api/channel-groups/${groupId}`, {
            method: 'DELETE'
        });
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification('Channel group deleted', 'success');
            await loadChannelGroups();
        } else {
            showNotification(result.error || 'Error deleting channel group', 'error');
        }
    } catch (error) {
        console.error('Error deleting channel group:', error);
        showNotification('Error deleting channel group', 'error');
    }
}

// ==================== PRESET LINKS ====================

let presetLinks = [];
let currentEditingLinkId = null;

function setupPresetLinksListeners() {
    document.getElementById('manage-preset-links-btn').addEventListener('click', () => {
        openPresetLinksModal();
    });
    
    document.getElementById('add-preset-link-btn').addEventListener('click', () => {
        openPresetLinkEditor();
    });
    
    document.getElementById('save-preset-link-btn').addEventListener('click', () => {
        savePresetLink();
    });
    
    document.getElementById('preset-links-modal').querySelectorAll('.modal-close').forEach(btn => {
        btn.addEventListener('click', () => {
            document.getElementById('preset-links-modal').classList.add('hidden');
        });
    });
    
    document.getElementById('preset-link-editor-modal').querySelectorAll('.modal-close').forEach(btn => {
        btn.addEventListener('click', () => {
            document.getElementById('preset-link-editor-modal').classList.add('hidden');
        });
    });
}

async function openPresetLinksModal() {
    await loadPresets(); // Ensure presets are up-to-date
    await loadPresetLinks();
    document.getElementById('preset-links-modal').classList.remove('hidden');
}

async function loadPresetLinks() {
    try {
        const response = await fetch('/api/preset-links');
        const data = await response.json();
        presetLinks = data.preset_links || [];
        renderPresetLinks();
    } catch (error) {
        console.error('Error loading preset links:', error);
        showNotification('Error loading preset links', 'error');
    }
}

function renderPresetLinks() {
    const container = document.getElementById('preset-links-list');
    
    if (presetLinks.length === 0) {
        container.innerHTML = '<div class="empty-state">No preset links created yet.</div>';
        return;
    }
    
    container.innerHTML = presetLinks.map(link => `
        <div class="item-card">
            <div class="item-info">
                <h4>${escapeHtml(link.name || link.id)}</h4>
                <p>${escapeHtml(link.description || '')}</p>
                <p><small>${link.source_preset} → ${link.target_node} (${link.target_preset})</small></p>
            </div>
            <div class="item-actions">
                <button class="btn btn-small btn-secondary" onclick="editPresetLink('${link.id}')">Edit</button>
                <button class="btn btn-small btn-danger" onclick="deletePresetLink('${link.id}')">Delete</button>
            </div>
        </div>
    `).join('');
}

function openPresetLinkEditor(link = null) {
    currentEditingLinkId = link ? link.id : null;
    
    // Populate preset dropdown
    const sourcePresetSelect = document.getElementById('link-source-preset');
    sourcePresetSelect.innerHTML = '<option value="">Select preset...</option>' +
        presets.map(p => `<option value="${p.preset_name}">${escapeHtml(p.description)}</option>`).join('');
    
    document.getElementById('link-id').value = link ? link.id : '';
    document.getElementById('link-name').value = link ? (link.name || '') : '';
    document.getElementById('link-description').value = link ? (link.description || '') : '';
    document.getElementById('link-source-preset').value = link ? link.source_preset : '';
    document.getElementById('link-target-node').value = link ? link.target_node : '';
    document.getElementById('link-target-preset').value = link ? link.target_preset : '';
    document.getElementById('link-type').value = link ? (link.link_type || 'trigger') : 'trigger';
    document.getElementById('link-delay').value = link ? (link.delay_ms || 0) : 0;
    document.getElementById('link-enabled').checked = link ? link.enabled !== false : true;
    
    document.getElementById('preset-link-editor-title').textContent = link ? 'Edit Preset Link' : 'Add Preset Link';
    document.getElementById('preset-link-editor-modal').classList.remove('hidden');
}

async function editPresetLink(linkId) {
    const link = presetLinks.find(l => l.id === linkId);
    if (link) {
        openPresetLinkEditor(link);
    }
}

async function savePresetLink() {
    // Generate a more robust ID using timestamp + random component
    const generateId = () => `link-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
    
    const linkData = {
        id: document.getElementById('link-id').value || generateId(),
        name: document.getElementById('link-name').value,
        description: document.getElementById('link-description').value,
        source_preset: document.getElementById('link-source-preset').value,
        target_node: document.getElementById('link-target-node').value,
        target_preset: document.getElementById('link-target-preset').value,
        link_type: document.getElementById('link-type').value,
        delay_ms: parseInt(document.getElementById('link-delay').value) || 0,
        enabled: document.getElementById('link-enabled').checked
    };
    
    if (!linkData.source_preset || !linkData.target_node || !linkData.target_preset) {
        showNotification('Please fill in required fields', 'error');
        return;
    }
    
    try {
        let response;
        if (currentEditingLinkId) {
            response = await fetch(`/api/preset-links/${currentEditingLinkId}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(linkData)
            });
        } else {
            response = await fetch('/api/preset-links', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(linkData)
            });
        }
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification(currentEditingLinkId ? 'Preset link updated' : 'Preset link created', 'success');
            document.getElementById('preset-link-editor-modal').classList.add('hidden');
            await loadPresetLinks();
        } else {
            showNotification(result.error || 'Error saving preset link', 'error');
        }
    } catch (error) {
        console.error('Error saving preset link:', error);
        showNotification('Error saving preset link', 'error');
    }
}

async function deletePresetLink(linkId) {
    if (!confirm('Are you sure you want to delete this preset link?')) {
        return;
    }
    
    try {
        const response = await fetch(`/api/preset-links/${linkId}`, {
            method: 'DELETE'
        });
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification('Preset link deleted', 'success');
            await loadPresetLinks();
        } else {
            showNotification(result.error || 'Error deleting preset link', 'error');
        }
    } catch (error) {
        console.error('Error deleting preset link:', error);
        showNotification('Error deleting preset link', 'error');
    }
}

// ==================== CONDITIONAL LOGIC ====================

let conditionalRules = [];
let currentEditingRuleId = null;
let conditionCounter = 0;

function setupConditionalRulesListeners() {
    document.getElementById('manage-conditional-rules-btn').addEventListener('click', () => {
        openConditionalRulesModal();
    });
    
    document.getElementById('add-conditional-rule-btn').addEventListener('click', () => {
        openConditionalRuleEditor();
    });
    
    document.getElementById('save-conditional-rule-btn').addEventListener('click', () => {
        saveConditionalRule();
    });
    
    document.getElementById('add-condition-btn').addEventListener('click', () => {
        addConditionRow();
    });
    
    document.getElementById('conditional-rules-modal').querySelectorAll('.modal-close').forEach(btn => {
        btn.addEventListener('click', () => {
            document.getElementById('conditional-rules-modal').classList.add('hidden');
        });
    });
    
    document.getElementById('conditional-rule-editor-modal').querySelectorAll('.modal-close').forEach(btn => {
        btn.addEventListener('click', () => {
            document.getElementById('conditional-rule-editor-modal').classList.add('hidden');
        });
    });
}

async function openConditionalRulesModal() {
    await loadPresets(); // Ensure presets are up-to-date
    await loadConditionalRules();
    document.getElementById('conditional-rules-modal').classList.remove('hidden');
}

async function loadConditionalRules() {
    try {
        const response = await fetch('/api/conditional-rules');
        const data = await response.json();
        conditionalRules = data.conditional_rules || [];
        renderConditionalRules();
    } catch (error) {
        console.error('Error loading conditional rules:', error);
        showNotification('Error loading conditional rules', 'error');
    }
}

function renderConditionalRules() {
    const container = document.getElementById('conditional-rules-list');
    
    if (conditionalRules.length === 0) {
        container.innerHTML = '<div class="empty-state">No conditional rules created yet.</div>';
        return;
    }
    
    container.innerHTML = conditionalRules.map(rule => `
        <div class="item-card">
            <div class="item-info">
                <h4>${escapeHtml(rule.name)}</h4>
                <p>${escapeHtml(rule.description || '')}</p>
                <p><small>${rule.conditions.length} condition(s), ${rule.logic_operator}</small></p>
            </div>
            <div class="item-actions">
                <button class="btn btn-small btn-secondary" onclick="editConditionalRule('${rule.id}')">Edit</button>
                <button class="btn btn-small btn-danger" onclick="deleteConditionalRule('${rule.id}')">Delete</button>
            </div>
        </div>
    `).join('');
}

function openConditionalRuleEditor(rule = null) {
    currentEditingRuleId = rule ? rule.id : null;
    conditionCounter = 0;
    
    // Populate preset dropdowns
    const actionPresetSelect = document.getElementById('action-true-preset');
    actionPresetSelect.innerHTML = '<option value="">Select preset...</option>' +
        presets.map(p => `<option value="${p.preset_name}">${escapeHtml(p.description)}</option>`).join('');
    
    document.getElementById('rule-id').value = rule ? rule.id : '';
    document.getElementById('rule-name').value = rule ? rule.name : '';
    document.getElementById('rule-description').value = rule ? (rule.description || '') : '';
    document.getElementById('logic-operator').value = rule ? (rule.logic_operator || 'AND') : 'AND';
    document.getElementById('rule-enabled').checked = rule ? rule.enabled !== false : true;
    
    // Clear and populate conditions
    const conditionsContainer = document.getElementById('conditions-container');
    conditionsContainer.innerHTML = '';
    
    if (rule && rule.conditions && rule.conditions.length > 0) {
        rule.conditions.forEach(condition => {
            addConditionRow(condition);
        });
    } else {
        addConditionRow();
    }
    
    // Set action
    if (rule && rule.action_if_true) {
        document.getElementById('action-true-type').value = rule.action_if_true.type || 'play_preset';
        document.getElementById('action-true-preset').value = rule.action_if_true.preset_name || '';
    }
    
    document.getElementById('conditional-rule-editor-title').textContent = rule ? 'Edit Conditional Rule' : 'Add Conditional Rule';
    document.getElementById('conditional-rule-editor-modal').classList.remove('hidden');
}

function addConditionRow(condition = null) {
    const container = document.getElementById('conditions-container');
    const conditionId = conditionCounter++;
    
    const conditionRow = document.createElement('div');
    conditionRow.className = 'condition-row';
    conditionRow.dataset.conditionId = conditionId;
    
    conditionRow.innerHTML = `
        <div class="form-group">
            <label>Condition Type</label>
            <select class="form-control condition-type" data-id="${conditionId}">
                <option value="sensor">Sensor</option>
                <option value="time">Time</option>
                <option value="system_state">System State</option>
            </select>
        </div>
        <div class="form-group condition-sensor-type">
            <label>Sensor Type</label>
            <select class="form-control condition-sensor">
                <option value="motion">Motion</option>
                <option value="temperature">Temperature</option>
                <option value="humidity">Humidity</option>
                <option value="light">Light Level</option>
            </select>
        </div>
        <div class="form-group">
            <label>Operator</label>
            <select class="form-control condition-operator">
                <option value="equals">Equals</option>
                <option value="greater_than">Greater Than</option>
                <option value="less_than">Less Than</option>
                <option value="in_range">In Range</option>
            </select>
        </div>
        <div class="form-group condition-value-field">
            <label>Value</label>
            <input type="text" class="form-control condition-value" placeholder="Value">
        </div>
        <button type="button" class="btn btn-danger btn-small" onclick="removeConditionRow(${conditionId})">Remove</button>
    `;
    
    container.appendChild(conditionRow);
    
    // Set values if editing
    if (condition) {
        conditionRow.querySelector('.condition-type').value = condition.type || 'sensor';
        if (condition.sensor_type) {
            conditionRow.querySelector('.condition-sensor').value = condition.sensor_type;
        }
        conditionRow.querySelector('.condition-operator').value = condition.operator || 'equals';
        if (condition.value !== undefined) {
            conditionRow.querySelector('.condition-value').value = condition.value;
        }
    }
    
    // Add event listener for type change
    conditionRow.querySelector('.condition-type').addEventListener('change', (e) => {
        updateConditionFields(conditionId);
    });
    
    updateConditionFields(conditionId);
}

function updateConditionFields(conditionId) {
    const conditionRow = document.querySelector(`[data-condition-id="${conditionId}"]`);
    if (!conditionRow) return;
    
    const type = conditionRow.querySelector('.condition-type').value;
    const sensorTypeField = conditionRow.querySelector('.condition-sensor-type');
    
    // Show/hide sensor type field based on condition type
    if (type === 'sensor') {
        sensorTypeField.style.display = 'block';
    } else {
        sensorTypeField.style.display = 'none';
    }
}

function removeConditionRow(conditionId) {
    const conditionRow = document.querySelector(`[data-condition-id="${conditionId}"]`);
    if (conditionRow) {
        conditionRow.remove();
    }
}

async function editConditionalRule(ruleId) {
    const rule = conditionalRules.find(r => r.id === ruleId);
    if (rule) {
        openConditionalRuleEditor(rule);
    }
}

async function saveConditionalRule() {
    const conditions = [];
    document.querySelectorAll('.condition-row').forEach(row => {
        const condition = {
            type: row.querySelector('.condition-type').value,
            operator: row.querySelector('.condition-operator').value
        };
        
        if (condition.type === 'sensor') {
            condition.sensor_type = row.querySelector('.condition-sensor').value;
        }
        
        const value = row.querySelector('.condition-value').value;
        if (value) {
            // Better type checking: if it's a valid number, convert it, otherwise keep as string
            const numValue = Number(value);
            condition.value = !isNaN(numValue) && value !== '' ? numValue : value;
        }
        
        conditions.push(condition);
    });
    
    const actionType = document.getElementById('action-true-type').value;
    const actionIfTrue = {
        type: actionType
    };
    
    if (actionType === 'play_preset') {
        actionIfTrue.preset_name = document.getElementById('action-true-preset').value;
    }
    
    // Generate a more robust ID using timestamp + random component
    const generateId = () => `rule-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
    
    const ruleData = {
        id: document.getElementById('rule-id').value || generateId(),
        name: document.getElementById('rule-name').value,
        description: document.getElementById('rule-description').value,
        conditions: conditions,
        logic_operator: document.getElementById('logic-operator').value,
        action_if_true: actionIfTrue,
        enabled: document.getElementById('rule-enabled').checked
    };
    
    if (!ruleData.name || conditions.length === 0) {
        showNotification('Please fill in required fields and add at least one condition', 'error');
        return;
    }
    
    try {
        let response;
        if (currentEditingRuleId) {
            response = await fetch(`/api/conditional-rules/${currentEditingRuleId}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(ruleData)
            });
        } else {
            response = await fetch('/api/conditional-rules', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(ruleData)
            });
        }
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification(currentEditingRuleId ? 'Conditional rule updated' : 'Conditional rule created', 'success');
            document.getElementById('conditional-rule-editor-modal').classList.add('hidden');
            await loadConditionalRules();
        } else {
            showNotification(result.error || 'Error saving conditional rule', 'error');
        }
    } catch (error) {
        console.error('Error saving conditional rule:', error);
        showNotification('Error saving conditional rule', 'error');
    }
}

async function deleteConditionalRule(ruleId) {
    if (!confirm('Are you sure you want to delete this conditional rule?')) {
        return;
    }
    
    try {
        const response = await fetch(`/api/conditional-rules/${ruleId}`, {
            method: 'DELETE'
        });
        
        const result = await response.json();
        
        if (response.ok) {
            showNotification('Conditional rule deleted', 'success');
            await loadConditionalRules();
        } else {
            showNotification(result.error || 'Error deleting conditional rule', 'error');
        }
    } catch (error) {
        console.error('Error deleting conditional rule:', error);
        showNotification('Error deleting conditional rule', 'error');
    }
}

// ==================== VISUAL TIMELINE EDITOR ====================

function updateTimelineSelector() {
    const select = document.getElementById('timeline-select');
    if (!select) return;
    
    select.innerHTML = '<option value="">Select a timeline...</option>' +
        timelines.map(t => `<option value="${t.id}">${escapeHtml(t.name)}</option>`).join('');
    
    if (selectedTimelineId) {
        select.value = selectedTimelineId;
    }
}

function selectTimeline(timelineId) {
    selectedTimelineId = timelineId;
    selectedBlockIndex = null;
    
    const emptyState = document.getElementById('timeline-empty-state');
    const propertiesPanel = document.getElementById('block-properties-panel');
    
    if (timelineId) {
        if (emptyState) emptyState.classList.add('hidden');
        renderVisualTimeline();
    } else {
        if (emptyState) emptyState.classList.remove('hidden');
        clearVisualTimeline();
    }
    
    if (propertiesPanel) propertiesPanel.classList.add('hidden');
}

function setZoom(newZoom) {
    zoomLevel = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, newZoom));
    const zoomLevelEl = document.getElementById('zoom-level');
    if (zoomLevelEl) {
        zoomLevelEl.textContent = `${zoomLevel}%`;
    }
    renderTimeRuler();
    if (selectedTimelineId) {
        renderVisualTimeline();
    }
}

function getPixelsPerMinute() {
    return PIXELS_PER_MINUTE * (zoomLevel / 100);
}

function renderTimeRuler() {
    const ruler = document.getElementById('time-ruler-track');
    if (!ruler) return;
    
    const ppm = getPixelsPerMinute();
    const totalMinutes = 24 * 60; // 24 hours
    const totalWidth = totalMinutes * ppm;
    
    ruler.style.width = `${totalWidth}px`;
    ruler.innerHTML = '';
    
    // Create time markers every 30 minutes
    for (let i = 0; i <= totalMinutes; i += 30) {
        const marker = document.createElement('div');
        marker.className = `time-marker ${i % 60 === 0 ? 'major' : ''}`;
        marker.style.left = `${i * ppm}px`;
        
        if (i % 60 === 0) {
            const hours = Math.floor(i / 60);
            marker.textContent = `${hours.toString().padStart(2, '0')}:00`;
        }
        
        ruler.appendChild(marker);
    }
}

function clearVisualTimeline() {
    const sidebar = document.getElementById('tracks-sidebar');
    const content = document.getElementById('tracks-content');
    
    if (sidebar) sidebar.innerHTML = '';
    if (content) content.innerHTML = '';
}

function renderVisualTimeline() {
    const timeline = timelines.find(t => t.id === selectedTimelineId);
    if (!timeline) return;
    
    const sidebar = document.getElementById('tracks-sidebar');
    const content = document.getElementById('tracks-content');
    
    if (!sidebar || !content) return;
    
    sidebar.innerHTML = '';
    content.innerHTML = '';
    
    const ppm = getPixelsPerMinute();
    const totalWidth = 24 * 60 * ppm;
    
    content.style.width = `${totalWidth}px`;
    
    // Group entries by node/channel (for now, use a single track)
    // In a full implementation, entries would be grouped by node type
    const tracks = [{ name: 'Main Track', icon: '🎬', entries: timeline.entries }];
    
    tracks.forEach((track, trackIndex) => {
        // Track label
        const label = document.createElement('div');
        label.className = 'track-label';
        label.innerHTML = `
            <span class="track-label-icon">${track.icon}</span>
            <span class="track-label-text">${escapeHtml(track.name)}</span>
        `;
        sidebar.appendChild(label);
        
        // Track row
        const row = document.createElement('div');
        row.className = 'track-row';
        row.dataset.trackIndex = trackIndex;
        content.appendChild(row);
        
        // Render blocks
        track.entries.forEach((entry, entryIndex) => {
            const block = createTimelineBlock(entry, entryIndex, trackIndex);
            row.appendChild(block);
        });
    });
}

function createTimelineBlock(entry, entryIndex, trackIndex) {
    const ppm = getPixelsPerMinute();
    
    // Calculate position based on trigger time
    let startMinutes = 0;
    if (entry.time_of_day) {
        const parts = entry.time_of_day.split(':');
        startMinutes = parseInt(parts[0]) * 60 + parseInt(parts[1]);
    } else if (entry.datetime) {
        const date = new Date(entry.datetime);
        startMinutes = date.getHours() * 60 + date.getMinutes();
    }
    
    // Calculate width based on duration (default to 60 minutes if 0)
    const durationMinutes = entry.duration > 0 ? entry.duration / 60 : 60;
    const width = Math.max(40, durationMinutes * ppm);
    
    const block = document.createElement('div');
    block.className = 'timeline-block';
    block.dataset.entryIndex = entryIndex;
    block.dataset.trackIndex = trackIndex;
    block.style.left = `${startMinutes * ppm}px`;
    block.style.width = `${width}px`;
    
    // Set color based on preset type
    const preset = presets.find(p => p.preset_name === entry.preset_name);
    if (preset && preset.color) {
        block.style.background = preset.color;
    }
    
    if (!entry.enabled) {
        block.style.opacity = '0.5';
    }
    
    block.innerHTML = `
        <div class="resize-handle resize-handle-left" data-direction="left"></div>
        <span class="timeline-block-content">${escapeHtml(entry.preset_name || 'Empty')}</span>
        <div class="resize-handle resize-handle-right" data-direction="right"></div>
    `;
    
    // Event listeners
    block.addEventListener('click', (e) => {
        if (!isDragging && !isResizing) {
            selectBlock(entryIndex);
        }
    });
    
    block.addEventListener('mousedown', (e) => {
        if (e.target.classList.contains('resize-handle')) {
            startResize(e, block, e.target.dataset.direction);
        } else {
            startDrag(e, block);
        }
    });
    
    // Accessibility
    block.setAttribute('role', 'gridcell');
    block.setAttribute('tabindex', '0');
    block.setAttribute('aria-label', `${entry.preset_name || 'Empty block'}, ${entry.time_of_day || ''}`);
    
    return block;
}

function selectBlock(entryIndex) {
    selectedBlockIndex = entryIndex;
    
    // Update visual selection
    document.querySelectorAll('.timeline-block').forEach(block => {
        block.classList.remove('selected');
    });
    
    const selectedBlock = document.querySelector(`.timeline-block[data-entry-index="${entryIndex}"]`);
    if (selectedBlock) {
        selectedBlock.classList.add('selected');
        selectedBlock.focus();
    }
    
    // Show properties panel
    showBlockProperties(entryIndex);
}

function showBlockProperties(entryIndex) {
    const timeline = timelines.find(t => t.id === selectedTimelineId);
    if (!timeline || !timeline.entries[entryIndex]) return;
    
    const entry = timeline.entries[entryIndex];
    const panel = document.getElementById('block-properties-panel');
    
    if (!panel) return;
    
    // Populate fields
    const presetSelect = document.getElementById('block-preset');
    const startInput = document.getElementById('block-start');
    const durationInput = document.getElementById('block-duration');
    const enabledCheckbox = document.getElementById('block-enabled');
    
    if (presetSelect) presetSelect.value = entry.preset_name || '';
    if (startInput) startInput.value = entry.time_of_day ? entry.time_of_day.substring(0, 5) : '';
    if (durationInput) durationInput.value = entry.duration || 0;
    if (enabledCheckbox) enabledCheckbox.checked = entry.enabled !== false;
    
    panel.classList.remove('hidden');
}

function applyBlockProperties() {
    if (selectedBlockIndex === null || !selectedTimelineId) return;
    
    const timeline = timelines.find(t => t.id === selectedTimelineId);
    if (!timeline) return;
    
    const entry = timeline.entries[selectedBlockIndex];
    if (!entry) return;
    
    const presetSelect = document.getElementById('block-preset');
    const startInput = document.getElementById('block-start');
    const durationInput = document.getElementById('block-duration');
    const enabledCheckbox = document.getElementById('block-enabled');
    
    if (presetSelect) entry.preset_name = presetSelect.value;
    if (startInput) entry.time_of_day = startInput.value + ':00';
    if (durationInput) entry.duration = parseInt(durationInput.value) || 0;
    if (enabledCheckbox) entry.enabled = enabledCheckbox.checked;
    
    // Save and refresh
    saveTimelineChanges();
}

function deleteSelectedBlock() {
    if (selectedBlockIndex === null || !selectedTimelineId) return;
    
    const timeline = timelines.find(t => t.id === selectedTimelineId);
    if (!timeline) return;
    
    if (!confirm('Delete this block?')) return;
    
    timeline.entries.splice(selectedBlockIndex, 1);
    selectedBlockIndex = null;
    
    const panel = document.getElementById('block-properties-panel');
    if (panel) panel.classList.add('hidden');
    
    saveTimelineChanges();
}

// ==================== DRAG AND DROP ====================

function startDrag(e, block) {
    if (e.button !== 0) return;
    e.preventDefault();
    
    isDragging = true;
    currentDragBlock = block;
    dragStartX = e.clientX;
    dragStartBlockLeft = parseInt(block.style.left) || 0;
    
    block.classList.add('dragging');
}

function startResize(e, block, direction) {
    if (e.button !== 0) return;
    e.preventDefault();
    e.stopPropagation();
    
    isResizing = true;
    resizeDirection = direction;
    currentDragBlock = block;
    dragStartX = e.clientX;
    dragStartBlockLeft = parseInt(block.style.left) || 0;
    dragStartBlockWidth = parseInt(block.style.width) || 40;
    
    block.classList.add('dragging');
}

function handleMouseMove(e) {
    if (!isDragging && !isResizing) return;
    if (!currentDragBlock) return;
    
    const deltaX = e.clientX - dragStartX;
    const ppm = getPixelsPerMinute();
    
    if (isDragging) {
        const newLeft = Math.max(0, dragStartBlockLeft + deltaX);
        currentDragBlock.style.left = `${newLeft}px`;
    } else if (isResizing) {
        if (resizeDirection === 'right') {
            const newWidth = Math.max(40, dragStartBlockWidth + deltaX);
            currentDragBlock.style.width = `${newWidth}px`;
        } else if (resizeDirection === 'left') {
            const newLeft = Math.max(0, dragStartBlockLeft + deltaX);
            const newWidth = Math.max(40, dragStartBlockWidth - deltaX);
            currentDragBlock.style.left = `${newLeft}px`;
            currentDragBlock.style.width = `${newWidth}px`;
        }
    }
}

function handleMouseUp(e) {
    if (!isDragging && !isResizing) return;
    
    if (currentDragBlock) {
        currentDragBlock.classList.remove('dragging');
        
        // Update the entry data based on new position/size
        const entryIndex = parseInt(currentDragBlock.dataset.entryIndex);
        updateEntryFromBlock(entryIndex, currentDragBlock);
    }
    
    isDragging = false;
    isResizing = false;
    currentDragBlock = null;
    resizeDirection = null;
}

function updateEntryFromBlock(entryIndex, block) {
    const timeline = timelines.find(t => t.id === selectedTimelineId);
    if (!timeline || !timeline.entries[entryIndex]) return;
    
    const entry = timeline.entries[entryIndex];
    const ppm = getPixelsPerMinute();
    
    // Calculate time from position
    const left = parseInt(block.style.left) || 0;
    const totalMinutes = Math.round(left / ppm);
    const hours = Math.floor(totalMinutes / 60);
    const minutes = totalMinutes % 60;
    
    entry.time_of_day = `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:00`;
    
    // Calculate duration from width
    const width = parseInt(block.style.width) || 40;
    const durationMinutes = Math.round(width / ppm);
    entry.duration = durationMinutes * 60;
    
    // Save changes
    saveTimelineChanges();
}

// ==================== KEYBOARD NAVIGATION ====================

function handleKeyboardNavigation(e) {
    if (!selectedTimelineId) return;
    
    const blocks = Array.from(document.querySelectorAll('.timeline-block'));
    if (blocks.length === 0) return;
    
    let currentIndex = selectedBlockIndex !== null ? selectedBlockIndex : -1;
    
    switch (e.key) {
        case 'ArrowLeft':
            e.preventDefault();
            if (currentIndex > 0) {
                selectBlock(currentIndex - 1);
            }
            break;
        case 'ArrowRight':
            e.preventDefault();
            if (currentIndex < blocks.length - 1) {
                selectBlock(currentIndex + 1);
            }
            break;
        case 'Enter':
        case ' ':
            e.preventDefault();
            if (currentIndex >= 0) {
                showBlockProperties(currentIndex);
            }
            break;
        case 'Delete':
        case 'Backspace':
            e.preventDefault();
            if (currentIndex >= 0) {
                deleteSelectedBlock();
            }
            break;
    }
}

// ==================== TRACK AND BLOCK MANAGEMENT ====================

function addTrack() {
    if (!selectedTimelineId) {
        showNotification('Please select a timeline first', 'error');
        return;
    }
    
    // For now, we just refresh the view
    // A full implementation would support multiple tracks per timeline
    renderVisualTimeline();
    showNotification('Track added', 'success');
}

function addBlock() {
    if (!selectedTimelineId) {
        showNotification('Please select a timeline first', 'error');
        return;
    }
    
    const timeline = timelines.find(t => t.id === selectedTimelineId);
    if (!timeline) return;
    
    // Create a new entry at the default position
    const newEntry = {
        id: `entry-${Date.now()}`,
        preset_name: '',
        trigger_type: 'time_of_day',
        time_of_day: '12:00:00',
        days_of_week: [0, 1, 2, 3, 4, 5, 6],
        duration: 3600, // 1 hour
        enabled: true
    };
    
    timeline.entries.push(newEntry);
    
    // Select the new block
    selectedBlockIndex = timeline.entries.length - 1;
    
    // Save and refresh
    saveTimelineChanges();
}

async function saveTimelineChanges() {
    const timeline = timelines.find(t => t.id === selectedTimelineId);
    if (!timeline) return;
    
    try {
        const response = await fetch(`/api/timelines/${selectedTimelineId}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(timeline)
        });
        
        if (response.ok) {
            renderVisualTimeline();
            if (selectedBlockIndex !== null) {
                selectBlock(selectedBlockIndex);
            }
        } else {
            const result = await response.json();
            showNotification(result.error || 'Error saving changes', 'error');
        }
    } catch (error) {
        console.error('Error saving timeline:', error);
        showNotification('Error saving changes', 'error');
    }
}

// ==================== PLAYBACK CONTROLS ====================

async function startPlayback() {
    if (!selectedTimelineId) {
        showNotification('Please select a timeline first', 'error');
        return;
    }
    
    try {
        const response = await fetch(`/api/timelines/${selectedTimelineId}/activate`, {
            method: 'POST'
        });
        
        if (response.ok) {
            showNotification('Playback started', 'success');
            await loadSequencerStatus();
        } else {
            const result = await response.json();
            showNotification(result.error || 'Error starting playback', 'error');
        }
    } catch (error) {
        console.error('Error starting playback:', error);
        showNotification('Error starting playback', 'error');
    }
}

async function stopPlayback() {
    if (!selectedTimelineId) return;
    
    try {
        const response = await fetch(`/api/timelines/${selectedTimelineId}/deactivate`, {
            method: 'POST'
        });
        
        if (response.ok) {
            showNotification('Playback stopped', 'success');
            await loadSequencerStatus();
        } else {
            const result = await response.json();
            showNotification(result.error || 'Error stopping playback', 'error');
        }
    } catch (error) {
        console.error('Error stopping playback:', error);
        showNotification('Error stopping playback', 'error');
    }
}

function rewindPlayback() {
    // Reset playhead to beginning
    const playhead = document.getElementById('playhead');
    if (playhead) {
        playhead.style.left = '0px';
    }
    showNotification('Playhead reset to start', 'success');
}

// ==================== CALENDAR SCHEDULING ====================

function addExceptionDate() {
    const input = document.getElementById('new-exception-date');
    if (!input || !input.value) return;
    
    const dateValue = input.value;
    const list = document.getElementById('exception-dates-list');
    
    if (!list) return;
    
    // Check if already exists
    if (list.querySelector(`[data-date="${dateValue}"]`)) {
        showNotification('Date already added', 'error');
        return;
    }
    
    const tag = document.createElement('span');
    tag.className = 'exception-date-tag';
    tag.dataset.date = dateValue;
    tag.innerHTML = `
        ${dateValue}
        <button type="button" onclick="removeExceptionDate('${dateValue}')" aria-label="Remove date">&times;</button>
    `;
    
    list.appendChild(tag);
    input.value = '';
}

function removeExceptionDate(date) {
    const tag = document.querySelector(`.exception-date-tag[data-date="${date}"]`);
    if (tag) {
        tag.remove();
    }
}

function applyCalendarSettings() {
    if (selectedBlockIndex === null || !selectedTimelineId) {
        showNotification('Please select a block first', 'error');
        return;
    }
    
    const timeline = timelines.find(t => t.id === selectedTimelineId);
    if (!timeline || !timeline.entries[selectedBlockIndex]) return;
    
    const entry = timeline.entries[selectedBlockIndex];
    
    // Get calendar settings
    const validFrom = document.getElementById('calendar-valid-from');
    const validUntil = document.getElementById('calendar-valid-until');
    const startTime = document.getElementById('calendar-start-time');
    const endTime = document.getElementById('calendar-end-time');
    
    if (validFrom) entry.valid_from = validFrom.value || null;
    if (validUntil) entry.valid_until = validUntil.value || null;
    if (startTime) entry.start_time = startTime.value ? startTime.value + ':00' : null;
    if (endTime) entry.end_time = endTime.value ? endTime.value + ':00' : null;
    
    // Get days of week
    const daysCheckboxes = document.querySelectorAll('.days-of-week-selector input[type="checkbox"]');
    entry.days_of_week = Array.from(daysCheckboxes)
        .filter(cb => cb.checked)
        .map(cb => parseInt(cb.value));
    
    // Get exception dates
    const exceptionTags = document.querySelectorAll('.exception-date-tag');
    entry.exception_dates = Array.from(exceptionTags).map(tag => tag.dataset.date);
    
    saveTimelineChanges();
    showNotification('Calendar settings applied', 'success');
}
