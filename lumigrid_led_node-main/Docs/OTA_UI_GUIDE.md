# OTA UI Implementation - Visual Guide

## Location
The OTA upload interface is located in the **Configuration** page of the web UI.

## UI Elements

### 1. Toggle Button
In the "Actions" section, there is a new button:
- **Label**: "Show OTA Update" / "Hide OTA Update" (toggles)
- **Style**: Warning button (btn-outline btn-warning)
- **Purpose**: Shows/hides the OTA upload section

### 2. OTA Upload Section (Hidden by Default)

When revealed, displays a prominent warning card with:

#### Header
- **Title**: "⚠️ OTA Firmware Update (Lab Use Only)"
- **Style**: Warning-themed card with yellow/orange border

#### Warning Alert
- **Icon**: Warning triangle
- **Message**: "Device will reboot after upload. Supports automatic rollback on failed boot."
- **Purpose**: Inform users of consequences

#### File Selection
- **Label**: "Select Firmware File (.bin)"
- **Input**: File input that only accepts `.bin` files
- **Validation**: Checks file extension, shows alert if wrong type
- **Disabled**: During upload

#### Selected File Info (appears when file is selected)
- **Icon**: Information icon
- **Content**: "Selected: [filename] ([size])"
- **Size Format**: Smart formatting (e.g., "1.2 MB" instead of "1234 KB")
- **Style**: Info alert (blue)

#### Upload Progress (appears during upload)
- **Display**: Progress bar with percentage
- **Labels**: "Uploading..." and "[X]%"
- **Style**: Warning-colored progress bar

#### Upload Button
- **Label**: "Upload Firmware" / "Uploading..." (changes during upload)
- **Style**: Warning button
- **States**:
  - Disabled when no file selected
  - Disabled during upload
  - Shows loading text during upload

## User Flow

1. User navigates to Configuration page
2. Clicks "Show OTA Update" button in Actions section
3. OTA section expands with warning message
4. User clicks "Choose File" and selects a `.bin` firmware file
5. File info shows selected file name and size
6. User clicks "Upload Firmware"
7. Confirmation dialog: "⚠️ WARNING: Device will reboot after upload. Continue?"
8. If confirmed, upload begins with progress bar
9. On completion: "✅ Firmware uploaded successfully! Device will reboot now."
10. Device reboots automatically

## Error Handling

### Client-Side
- Wrong file type: "Please select a .bin firmware file"
- No file selected: "Please select a firmware file first"
- Network error: "❌ Upload failed: Network error"
- HTTP error: "❌ Upload failed: [status text]"

### Server-Side (from firmware)
- 400 Bad Request: Invalid request
- 500 Internal Server Error: OTA update failed

## Code Structure

```svelte
<script>
  // State variables
  let showOtaUpload = false;      // Toggle visibility
  let otaFile = null;             // Selected file
  let uploading = false;          // Upload in progress
  let uploadProgress = 0;         // 0-100 percentage

  // Functions
  handleOtaFileSelect(event)      // Validate and store selected file
  uploadFirmware()                // Upload via XHR with progress
  toggleOtaUpload()               // Show/hide section
  formatFileSize(bytes)           // Smart size formatting
</script>

<!-- UI Structure -->
Actions Section
  └─ Show/Hide OTA Update button

OTA Section (conditional: {#if showOtaUpload})
  ├─ Warning card
  │   ├─ Header with warning icon
  │   ├─ Alert about reboot
  │   ├─ File input
  │   ├─ File info (conditional: {#if otaFile})
  │   ├─ Progress bar (conditional: {#if uploading})
  │   └─ Upload button
```

## Security Features

1. **File Type Validation**: Only `.bin` files accepted
2. **User Confirmation**: Explicit warning before upload
3. **Automatic Rollback**: Device rolls back if new firmware fails
4. **Visual Warnings**: Clear indication this is lab-only feature

## Accessibility

- Semantic HTML structure
- Clear labels for all inputs
- Visual feedback for all states
- Error messages explain what went wrong
- Progress indication during upload

## Styling

Uses DaisyUI classes:
- `btn`, `btn-warning`, `btn-outline` - Buttons
- `card`, `card-body` - Card container
- `alert`, `alert-warning`, `alert-info` - Alerts
- `file-input`, `file-input-bordered`, `file-input-warning` - File input
- `progress`, `progress-warning` - Progress bar

Custom styling:
- Warning-themed border: `border-2 border-warning`
- Semi-transparent background: `bg-warning bg-opacity-20`
- Proper spacing with Tailwind utilities

## Testing Recommendations

1. **File Type Validation**: Try uploading non-.bin files
2. **Size Display**: Test with various file sizes (bytes, KB, MB, GB)
3. **Progress**: Monitor XHR progress during upload
4. **Error Handling**: Test with device offline, wrong URL
5. **Rollback**: Upload intentionally broken firmware to test rollback
