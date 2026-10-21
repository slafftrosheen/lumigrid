<script>
  import { base } from '$app/paths';
</script>
<svelte:head>
  <title>Build and install firmware · LumiGrid</title>
  <meta name="description" content="Build LumiGrid firmware and choose the correct installation or OTA path for your ESP32 node." />
</svelte:head>
<div class="container section" lang="en">
  <a href={`${base}/`}>← LumiGrid</a>
  <h1>Build and install firmware</h1>
  <p>This checkout targets an ESP32 with 8 MB flash. It does not include a verified browser-install release bundle.</p>
  <div class="card">
    <h2>Start with a local build</h2>
    <p>Install the web dependencies, then build the firmware’s <code>factory</code> environment. The build generates the device interface and checks the partition layout.</p>
    <a href={`${base}/docs/GETTING_STARTED`}>Build and first-time setup →</a>
  </div>
  <div class="card">
    <h2>Choose the correct update path</h2>
    <p>An older partition table requires a wired migration. A node already on the current layout can use the Admin OTA workflow with a compatible application image. Verify the running version and boot status after updating.</p>
    <a href={`${base}/docs/FLASH_LAYOUT`}>Partition layout, migration and OTA →</a>
  </div>
</div>
<style>
  h1 { font-size:clamp(2rem,4vw,3rem); margin-block:1.5rem; }
  p { max-width:70ch; line-height:1.7; color:var(--muted); }
  .card { padding:1.5rem; margin-top:1.5rem; max-width:80ch; }
</style>
