<script>
  import { onMount } from 'svelte';
  import { config, system } from '$lib/api.js';
  
  let configuration = {};
  let isLoading = true;
  let error = null;
  let saving = false;
  
  async function loadConfig() {
    try {
      isLoading = true;
      error = null;
      configuration = await config.get();
    } catch (err) {
      error = err.message;
      console.error('Error loading config:', err);
    } finally {
      isLoading = false;
    }
  }
  
  async function saveConfig() {
    try {
      saving = true;
      await config.update(configuration);
      // Show success notification
      console.log('Configuration saved successfully');
    } catch (err) {
      error = err.message;
      console.error('Error saving config:', err);
    } finally {
      saving = false;
    }
  }
  
  onMount(loadConfig);
</script>

<div class="container mx-auto p-4">
  <h1 class="text-2xl font-bold mb-6">Configuration</h1>
  
  {#if error}
    <div class="alert alert-error mb-4">
      <div>Error: {error}</div>
    </div>
  {/if}
  
  {#if isLoading}
    <div class="loading loading-spinner loading-lg"></div>
  {:else}
    <div class="card bg-base-100 shadow-xl">
      <div class="card-body">
        <form on:submit|preventDefault={saveConfig}>
          <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
            <!-- Network Configuration -->
            <div class="card bg-base-200">
              <div class="card-body">
                <h3 class="card-title">Network</h3>
                
                <div class="form-control">
                  <label class="label">
                    <span class="label-text">Node ID</span>
                  </label>
                  <input 
                    type="text" 
                    class="input input-bordered"
                    bind:value={configuration.network?.node_id}
                  />
                </div>
                
                <div class="form-control">
                  <label class="label">
                    <span class="label-text">Hostname</span>
                  </label>
                  <input 
                    type="text" 
                    class="input input-bordered"
                    bind:value={configuration.network?.hostname}
                  />
                </div>
              </div>
            </div>
            
            <!-- Display Configuration -->
            <div class="card bg-base-200">
              <div class="card-body">
                <h3 class="card-title">Display</h3>
                
                <div class="form-control">
                  <label class="label">
                    <span class="label-text">Default Mode</span>
                  </label>
                  <select 
                    class="select select-bordered"
                    bind:value={configuration.display?.default_mode}
                  >
                    <option value="clock">Clock</option>
                    <option value="temperature">Temperature</option>
                    <option value="message">Message</option>
                    <option value="frames">Frames</option>
                    <option value="slideshow">Slideshow</option>
                  </select>
                </div>
                
                <div class="form-control">
                  <label class="label">
                    <span class="label-text">Brightness</span>
                  </label>
                  <input 
                    type="range" 
                    min="0" 
                    max="100" 
                    class="range"
                    bind:value={configuration.led_matrix?.brightness}
                  />
                  <span class="text-right label-text-alt">{configuration.led_matrix?.brightness || 0}%</span>
                </div>
              </div>
            </div>
            
            <!-- Control Configuration -->
            <div class="card bg-base-200">
              <div class="card-body">
                <h3 class="card-title">Control</h3>
                
                <div class="form-control">
                  <label class="label">
                    <span class="label-text">Control Mode</span>
                  </label>
                  <select 
                    class="select select-bordered"
                    bind:value={configuration.control?.mode}
                  >
                    <option value="independent">Independent</option>
                    <option value="sync">Synchronized</option>
                    <option value="external">External</option>
                  </select>
                </div>
                
                {#if configuration.control?.mode === 'sync'}
                <div class="form-control">
                  <label class="label">
                    <span class="label-text">Sync Role</span>
                  </label>
                  <select 
                    class="select select-bordered"
                    bind:value={configuration.control?.sync_role}
                  >
                    <option value="slave">Slave</option>
                    <option value="master">Master</option>
                  </select>
                </div>
                {/if}
              </div>
            </div>
            
            <!-- Time Configuration -->
            <div class="card bg-base-200">
              <div class="card-body">
                <h3 class="card-title">Time</h3>
                
                <div class="form-control">
                  <label class="label">
                    <span class="label-text">Timezone</span>
                  </label>
                  <input 
                    type="text" 
                    class="input input-bordered"
                    bind:value={configuration.time?.timezone}
                  />
                </div>
                
                <div class="form-control">
                  <label class="label">
                    <span class="label-text">Format</span>
                  </label>
                  <select 
                    class="select select-bordered"
                    bind:value={configuration.time?.format}
                  >
                    <option value="24h">24-hour</option>
                    <option value="12h">12-hour</option>
                  </select>
                </div>
              </div>
            </div>
          </div>
          
          <div class="card-actions justify-end mt-6">
            <button 
              type="submit" 
              class="btn btn-primary"
              disabled={saving}
            >
              {saving ? 'Saving...' : 'Save Configuration'}
            </button>
            <button 
              type="button" 
              class="btn btn-outline"
              on:click={loadConfig}
            >
              Cancel
            </button>
          </div>
        </form>
      </div>
    </div>
  {/if}
</div>