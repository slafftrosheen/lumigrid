<script>
  import { base } from '$app/paths';
</script>

<svelte:head>
  <title>LED Node setup and updates · LumiGrid</title>
  <meta name="description" content="First-time setup and safe software update guidance for LumiGrid LED Node." />
</svelte:head>

<div class="container section" lang="en">
  <a href={`${base}/`}>← LumiGrid LED Node</a>
  <p class="eyebrow">INSTALLER INFORMATION</p>
  <h1>LED Node setup and updates</h1>
  <p>This page is for installers and advanced maintenance. A verified one-click installer is not available here. Use the guides to prepare a compatible update and keep saved settings intact.</p>

  <div class="guide-grid">
    <article class="card">
      <h2>Starting with a new LED Node</h2>
      <p>Check the installation requirements, connect the controller and complete its first-time setup before adding lighting.</p>
      <a href={`${base}/docs/GETTING_STARTED`}>First-time setup guide →</a>
    </article>
    <article class="card">
      <h2>Updating an existing LED Node</h2>
      <p>Use the supported update path for your existing configuration. A fresh-install image is not a safe routine update because it can erase saved settings.</p>
      <a href={`${base}/docs/FLASH_LAYOUT`}>Software updates and recovery →</a>
    </article>
  </div>
</div>

<style>
  .eyebrow { margin-top:2rem; color:var(--accent-primary); font-size:.8rem; font-weight:700; letter-spacing:.15em; }
  h1 { font-size:clamp(2rem,4vw,3rem); margin-block:.6rem 1.5rem; }
  h2 { font-size:clamp(1.25rem,2vw,1.6rem); margin:0 0 .6rem; }
  p { max-width:70ch; line-height:1.7; color:var(--muted); }
  .guide-grid { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:1rem; margin-top:2rem; }
  .card { padding:clamp(1.3rem,2.5vw,2rem); }
  .card a { display:inline-block; margin-top:.5rem; color:var(--ink); text-underline-offset:4px; }
  @media(max-width:650px) { .guide-grid { grid-template-columns:1fr; } }
</style>
