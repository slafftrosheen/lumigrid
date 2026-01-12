<script>
  import { onMount } from 'svelte';
  import { config as configApi, ota, system } from '$lib/api.js';
  
  let config = {
    node_type: 'led-node',
    role: 'Slave',
    wifi_ssid: '',
    wifi_password: '',
    hostname: 'lumigrid',
    aled_channels: 8,
    pwm_channels: 8
  };

  let showOtaUpload = false;
  let otaFile = null;
  let uploading = false;
  let uploadProgress = 0;
  let isSaving = false;
  let isLoading = true;
  let errorMessage = '';
  let successMessage = '';

  async function loadConfig() {
    isLoading = true;
    errorMessage = '';
    try {
      const data = await configApi.get();
      config = { ...config, ...data };
    } catch (error) {
      console.error('Failed to load configuration:', error);
      errorMessage = 'Failed to load configuration';
    } finally {
      isLoading = false;
    }
  }

  async function saveConfig() {
    if (isSaving) return;
    isSaving = true;
    errorMessage = '';
    successMessage = '';
    
    try {
      await configApi.update(config);
      successMessage = 'Configuration saved successfully!';
      setTimeout(() => successMessage = '', 3000);
    } catch (error) {
      console.error('Failed to save configuration:', error);
      errorMessage = 'Failed to save configuration';
    } finally {
      isSaving = false;
    }
  }
  
  async function rebootDevice() {
    if (!confirm('Are you sure you want to reboot the device?')) return;
    
    try {
      await system.reboot();
      successMessage = 'Device is rebooting...';
    } catch (error) {
      console.error('Failed to reboot:', error);
      errorMessage = 'Failed to reboot device';
    }
  }
  
  async function factoryReset() {
    if (!confirm('⚠️ WARNING: This will reset ALL settings to factory defaults. Continue?')) return;
    
    try {
      await system.factoryReset();
      successMessage = 'Factory reset complete. Device will reboot.';
      await loadConfig();
    } catch (error) {
      console.error('Failed to reset:', error);
      errorMessage = 'Failed to perform factory reset';
    }
  }

  function handleOtaFileSelect(event) {
    const file = event.target.files[0];
    if (file && file.name.endsWith('.bin')) {
      otaFile = file;
      errorMessage = '';
    } else {
      alert('Please select a .bin firmware file');
      event.target.value = '';
      otaFile = null;
    }
  }

  async function uploadFirmware() {
    if (!otaFile) {
      alert('Please select a firmware file first');
      return;
    }

    if (!confirm('⚠️ WARNING: Device will reboot after upload. Continue?')) {
      return;
    }

    uploading = true;
    uploadProgress = 0;
    errorMessage = '';

    try {
      await ota.uploadFirmware(otaFile, (progress) => {
        uploadProgress = progress;
      });
      
      successMessage = '✅ Firmware uploaded successfully! Device will reboot now.';
      uploading = false;
      uploadProgress = 0;
      otaFile = null;
    } catch (error) {
      console.error('OTA upload failed:', error);
      errorMessage = '❌ Upload failed: ' + error.message;
      uploading = false;
      uploadProgress = 0;
    }
  }

  function toggleOtaUpload() {
    showOtaUpload = !showOtaUpload;
  }

  function formatFileSize(bytes) {
    if (bytes === 0) return '0 Bytes';
    const BYTES_PER_KB = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(BYTES_PER_KB));
    return Math.round(bytes / Math.pow(BYTES_PER_KB, i) * 100) / 100 + ' ' + sizes[i];
  }
  
  onMount(() => {
    loadConfig();
  });
</script>

<svelte:head>
  <title>Configuration - LumiGrid</title>
</svelte:head>

<div class="space-y-6">
  <h1 class="text-3xl font-bold">Configuration</h1>

  {#if errorMessage}
    <div class="alert alert-error">
      <span>{errorMessage}</span>
    </div>
  {/if}
  
  {#if successMessage}
    <div class="alert alert-success">
      <span>{successMessage}</span>
    </div>
  {/if}

  {#if isLoading}
    <div class="flex justify-center py-8">
      <span class="loading loading-spinner loading-lg"></span>
    </div>
  {:else}
  <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
    <!-- Network Settings -->
    <div class="card bg-base-200 shadow-xl">
      <div class="card-body">
        <h2 class="card-title">Network Settings</h2>
        
        <div class="form-control">
          <label class="label">
            <span class="label-text">Hostname</span>
          </label>
          <input 
            type="text" 
            bind:value={config.hostname}
            class="input input-bordered" 
            placeholder="lumigrid"
          />
        </div>

        <div class="form-control">
          <label class="label">
            <span class="label-text">Wi-Fi SSID</span>
          </label>
          <input 
            type="text" 
            bind:value={config.wifi_ssid}
            class="input input-bordered" 
          />
        </div>

        <div class="form-control">
          <label class="label">
            <span class="label-text">Wi-Fi Password</span>
          </label>
          <input 
            type="password" 
            bind:value={config.wifi_password}
            class="input input-bordered" 
          />
        </div>
      </div>
    </div>

    <!-- Node Settings -->
    <div class="card bg-base-200 shadow-xl">
      <div class="card-body">
        <h2 class="card-title">Node Settings</h2>
        
        <div class="form-control">
          <label class="label">
            <span class="label-text">Node Type</span>
          </label>
          <select bind:value={config.node_type} class="select select-bordered">
            <option value="led-node">LED Node</option>
            <option value="master">Master Controller</option>
          </select>
        </div>

        <div class="form-control">
          <label class="label">
            <span class="label-text">Role</span>
          </label>
          <select bind:value={config.role} class="select select-bordered">
            <option value="Master">Master</option>
            <option value="Slave">Slave</option>
            <option value="Standalone">Standalone</option>
          </select>
        </div>

        <div class="form-control">
          <label class="label">
            <span class="label-text">ALED Channels</span>
          </label>
          <input 
            type="number" 
            bind:value={config.aled_channels}
            class="input input-bordered" 
            min="1"
            max="8"
          />
        </div>

        <div class="form-control">
          <label class="label">
            <span class="label-text">PWM Channels</span>
          </label>
          <input 
            type="number" 
            bind:value={config.pwm_channels}
            class="input input-bordered" 
            min="1"
            max="16"
          />
        </div>
      </div>
    </div>
  </div>
  {/if}

  <!-- Actions -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">Actions</h2>
      <div class="flex flex-wrap gap-2">
        <button 
          class="btn btn-primary" 
          on:click={saveConfig}
          disabled={isSaving || isLoading}
        >
          {isSaving ? 'Saving...' : 'Save Configuration'}
        </button>
        <button 
          class="btn btn-outline"
          on:click={rebootDevice}
        >
          Reboot Device
        </button>
        <button 
          class="btn btn-outline btn-error"
          on:click={factoryReset}
        >
          Factory Reset
        </button>
        <button class="btn btn-outline btn-warning" on:click={toggleOtaUpload}>
          {showOtaUpload ? 'Hide' : 'Show'} OTA Update
        </button>
      </div>
    </div>
  </div>

  <!-- OTA Firmware Upload (Hidden by default, lab use only) -->
  {#if showOtaUpload}
  <div class="card bg-warning bg-opacity-20 shadow-xl border-2 border-warning">
    <div class="card-body">
      <h2 class="card-title text-warning">
        ⚠️ OTA Firmware Update (Lab Use Only)
      </h2>
      <div class="alert alert-warning">
        <svg xmlns="http://www.w3.org/2000/svg" class="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
        </svg>
        <span>Device will reboot after upload. Supports automatic rollback on failed boot.</span>
      </div>
      
      <div class="form-control">
        <label class="label">
          <span class="label-text font-semibold">Select Firmware File (.bin)</span>
        </label>
        <input 
          type="file" 
          accept=".bin"
          on:change={handleOtaFileSelect}
          disabled={uploading}
          class="file-input file-input-bordered file-input-warning w-full" 
        />
      </div>

      {#if otaFile}
      <div class="alert alert-info">
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="stroke-current shrink-0 w-6 h-6">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path>
        </svg>
        <span>Selected: {otaFile.name} ({formatFileSize(otaFile.size)})</span>
      </div>
      {/if}

      {#if uploading}
      <div class="space-y-2">
        <div class="flex justify-between text-sm">
          <span>Uploading...</span>
          <span>{uploadProgress}%</span>
        </div>
        <progress class="progress progress-warning w-full" value={uploadProgress} max="100"></progress>
      </div>
      {/if}

      <div class="card-actions justify-end mt-4">
        <button 
          class="btn btn-warning" 
          on:click={uploadFirmware}
          disabled={!otaFile || uploading}
        >
          {uploading ? 'Uploading...' : 'Upload Firmware'}
        </button>
      </div>
    </div>
  </div>
  {/if}
</div>
