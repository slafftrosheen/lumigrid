<svelte:head>
  <title>Firmware Flasher - LumiGrid</title>
  <script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
</svelte:head>

<div class="container section">
  <div class="flasher-header">
    <h1>LumiGrid Node Flasher</h1>
    <p>Connect your LumiGrid Node (LED Node Board) via USB and click below to install or update the firmware directly from your browser.</p>
  </div>

  <div class="flasher-box">
    <esp-web-install-button manifest="/latest_builds/manifest.json"></esp-web-install-button>
  </div>
</div>

<style>
  .flasher-header {
    text-align: center;
    margin-bottom: var(--spacing-mobile-xl);
  }

  .flasher-header h1 {
    font-size: var(--fs-display-lg);
    margin-bottom: var(--spacing-mobile-md);
  }

  .flasher-header p {
    color: var(--muted);
    max-width: 60ch;
    margin: 0 auto;
    font-size: var(--fs-body);
  }

  .flasher-box {
    display: flex;
    justify-content: center;
    align-items: center;
    padding: var(--spacing-mobile-xl);
    background: var(--glass-bg);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: var(--radius-lg);
    max-width: 600px;
    margin: 0 auto;
    min-height: 200px;
  }
</style>
