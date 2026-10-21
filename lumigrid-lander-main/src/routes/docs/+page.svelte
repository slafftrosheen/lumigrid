<script lang="ts">
  import { base } from '$app/paths';
  const sections = [
    { title: 'Set up and operate', pages: [
      ['GETTING_STARTED', 'Getting started', 'Build, connect and commission your first channel.'],
      ['LED_NODE', 'Hardware and channels', 'Pin map, output types and physical limits.'],
      ['ACCESS', 'Access and recovery', 'Admin keys, roles, sessions and recovery.'],
      ['CONFIGURATION', 'Configuration', 'Topology, network changes and modeled power.'],
      ['EFFECTS', 'Effects and playback', 'Scenes, automation, timelines and cues.'],
      ['PROTOCOLS', 'Protocols', 'Art-Net, sACN, sync, discovery and MQTT.'],
      ['UI', 'Dashboard', 'Device interface, external editors and live state.'],
      ['FLASH_LAYOUT', 'Firmware updates', 'Partitions, wired migration and OTA verification.']
    ]},
    { title: 'Develop and verify', pages: [
      ['ARCHITECTURE', 'Architecture', 'Service ownership and render flow.'],
      ['API_REFERENCE', 'HTTP API', 'Registered routes extracted from firmware.'],
      ['CONTRIBUTING', 'Development', 'Build commands and shared documentation workflow.'],
      ['HARDWARE_VALIDATION', 'Hardware validation', 'Measurements needed for installation acceptance.'],
      ['HARDWARE_CERTIFICATION', 'Certification tools', 'Internal test plans and evidence collection.'],
      ['PCA9685_BRINGUP', 'PCA9685 bring-up', 'Current I²C transport and PWM verification.'],
      ['RUNTIME_MEMORY_BUDGET', 'Runtime memory', 'Admission, diagnostics and build policy.'],
      ['SOURCE_OF_TRUTH', 'Source audit', 'Supported claims and known implementation gaps.'],
      ['ROADMAP', 'Open work', 'Unresolved work and required evidence.']
    ]}
  ];
</script>

<svelte:head>
  <title>LumiGrid documentation</title>
  <meta name="description" content="Source-backed setup, hardware, API, playback and validation guides for LumiGrid." />
</svelte:head>

<div class="container section" lang="en">
  <a href={`${base}/`}>← LumiGrid</a>
  <h1>LumiGrid documentation</h1>
  <p class="lead">The controller and web project share these guides. Current behavior follows the firmware code; hardware acceptance requires evidence from the actual installation. Guides are maintained in English.</p>
  {#each sections as section}
    <section>
      <h2>{section.title}</h2>
      <div class="docs-grid">
        {#each section.pages as page}
          <a class="card" href={`${base}/docs/${page[0]}`}><h3>{page[1]}</h3><p>{page[2]}</p></a>
        {/each}
      </div>
    </section>
  {/each}
  <p>Earlier reports remain available through the <a href={`${base}/docs/README`}>full index</a> and their original document URLs. They are labeled as historical and do not certify the current build.</p>
</div>

<style>
  h1 { font-size:clamp(2rem,5vw,3.5rem); margin-block:1.5rem; }
  h2 { margin:2.5rem 0 1rem; }
  .lead { max-width:70ch; color:var(--muted); line-height:1.7; }
  .docs-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(min(100%,260px),1fr)); gap:1rem; }
  .card { display:block; padding:1.4rem; color:var(--ink); text-decoration:none; }
  .card h3 { margin:0 0 .6rem; }
  .card p { color:var(--muted); line-height:1.6; margin:0; }
  a:focus-visible { outline:2px solid currentColor; outline-offset:4px; }
</style>
