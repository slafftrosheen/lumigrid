<script lang="ts">
  import { base } from '$app/paths';
  import { onMount } from 'svelte';
  import { lang } from '$lib/i18n';
  import { publicContent } from '$lib/publicContent';

  const languages = ['en', 'lv', 'ru', 'nl', 'de'] as const;
  let ready = false;
  $: copy = publicContent[$lang];
  onMount(() => { ready = true; });
</script>

<svelte:head>
  <title>{copy.startGuideTitle} · LumiGrid LED Node</title>
  <meta name="description" content={copy.startGuideIntro} />
  <meta property="og:title" content={copy.startGuideTitle} />
  <meta property="og:description" content={copy.startGuideIntro} />
</svelte:head>

<div class="container start-guide" lang={$lang}>
  <header class="start-header">
    <a class="back-link" href={`${base}/`}>← {copy.startGuideBack}</a>
    <label class="language">{copy.language}
      <select bind:value={$lang} disabled={!ready} aria-label={copy.language}>
        {#each languages as code}<option value={code}>{code.toUpperCase()}</option>{/each}
      </select>
    </label>
  </header>

  <section class="guide-hero" aria-labelledby="guide-title">
    <div>
      <p class="eyebrow">{copy.startGuideTag}</p>
      <h1 id="guide-title">{copy.startGuideTitle}</h1>
      <p class="lead">{copy.startGuideIntro}</p>
    </div>
    <aside class="ready-panel card" aria-labelledby="ready-title">
      <span class="step-icon" aria-hidden="true">✓</span>
      <h2 id="ready-title">{copy.startGuideReadyTitle}</h2>
      <p>{copy.startGuideReady}</p>
    </aside>
  </section>

  <ol class="guide-steps">
    {#each copy.startGuideSteps as step, index}
      <li>
        <span class="count" aria-hidden="true">{String(index + 1).padStart(2, '0')}</span>
        <div>
          <h2>{step[0]}</h2>
          <p>{step[1]}</p>
          {#if index === 2}
            <div class="device-address">
              <span>{copy.startGuideAddressTitle}</span>
              <a href="http://192.168.4.1/" target="_blank" rel="noopener noreferrer">192.168.4.1 <span aria-hidden="true">↗</span></a>
              <small>{copy.startGuideAddressNote}</small>
            </div>
          {/if}
        </div>
      </li>
    {/each}
  </ol>

  <section class="help-panel" aria-labelledby="help-title">
    <h2 id="help-title">{copy.startGuideHelpTitle}</h2>
    <p>{copy.startGuideHelp}</p>
  </section>

  <footer class="guide-footer">
    <div class="actions">
      <a class="btn primary" href={`${base}/dashboard`}>{copy.startGuideOpen} <span aria-hidden="true">→</span></a>
      <a class="btn" href={`${base}/docs/GETTING_STARTED`}>{copy.startGuideAdvanced} <span aria-hidden="true">↗</span></a>
    </div>
    <p>{copy.startGuideFootnote}</p>
    <a class="back-link" href={`${base}/`}>← {copy.startGuideBack}</a>
  </footer>
</div>

<style>
  .start-guide { padding-bottom:clamp(3rem,6vw,6rem); }
  .start-header { display:flex; align-items:center; justify-content:space-between; gap:1.25rem; padding-block:1.35rem; border-bottom:1px solid var(--glass-border); }
  .back-link { color:var(--ink); text-decoration:none; font-weight:600; }
  .back-link:hover { color:var(--accent-primary); }
  .language { display:flex; gap:.65rem; align-items:center; color:var(--muted); font-size:.85rem; }
  .language select { min-height:44px; border:1px solid var(--glass-border); border-radius:.7rem; padding:.55rem .75rem; color:var(--ink); background:var(--bg-1); }
  .guide-hero { display:grid; grid-template-columns:minmax(0,1.15fr) minmax(0,.85fr); gap:clamp(1.5rem,5vw,5rem); align-items:center; padding-block:clamp(3rem,7vw,6.5rem); }
  .eyebrow { font-size:.76rem; color:var(--accent-primary); letter-spacing:.14em; font-weight:700; }
  h1 { font-size:clamp(2.6rem,5vw,4.6rem); line-height:1.07; letter-spacing:-.05em; margin:.8rem 0 1.25rem; max-width:16ch; }
  h2 { font-size:clamp(1.25rem,2.3vw,1.65rem); letter-spacing:-.025em; line-height:1.3; margin:.35rem 0 .8rem; }
  p { color:var(--muted); line-height:1.75; max-width:70ch; }
  .lead { font-size:clamp(1.05rem,1.75vw,1.2rem); line-height:1.7; }
  .ready-panel { padding:clamp(1.5rem,3vw,2.5rem); border-top:2px solid var(--accent-primary); }
  .step-icon { display:inline-grid; place-items:center; width:44px; height:44px; border-radius:50%; border:1px solid var(--accent-primary); color:var(--accent-primary); font-weight:750; }
  .guide-steps { list-style:none; padding:0; margin:0; border-top:1px solid var(--glass-border); }
  .guide-steps li { display:grid; grid-template-columns:90px minmax(0,1fr); gap:clamp(1rem,4vw,3.5rem); padding:clamp(1.8rem,4vw,3rem) 0; border-bottom:1px solid var(--glass-border); }
  .guide-steps .count { color:var(--accent-primary); font-size:clamp(2rem,4vw,3.4rem); font-weight:650; line-height:1; letter-spacing:-.06em; }
  .guide-steps h2 { margin:0 0 .65rem; }
  .guide-steps p { margin:0; }
  .device-address { display:flex; flex-direction:column; align-items:flex-start; gap:.35rem; padding:1.15rem 1.3rem; margin-top:1.5rem; background:var(--glass-bg); border:1px solid var(--glass-border); border-radius:14px; max-width:460px; }
  .device-address > span { color:var(--muted); font-size:.85rem; }
  .device-address a { font-size:clamp(1.35rem,3vw,1.85rem); font-weight:700; color:var(--ink); text-underline-offset:5px; }
  .device-address small { color:var(--muted); line-height:1.55; }
  .help-panel { padding:clamp(1.5rem,4vw,3rem); margin-top:3rem; border-left:3px solid var(--accent-primary); background:var(--glass-bg); border-radius:0 14px 14px 0; }
  .help-panel h2 { margin-top:0; }
  .help-panel p { margin-bottom:0; }
  .guide-footer { margin-top:3rem; }
  .actions { display:flex; flex-wrap:wrap; align-items:center; gap:.75rem; }
  .actions .btn { justify-content:center; text-align:center; min-height:44px; }
  .guide-footer p { font-size:.86rem; margin:1.3rem 0 2rem; }
  a:focus-visible, select:focus-visible { outline:2px solid var(--accent-primary); outline-offset:4px; }
  @media(max-width:750px) { .guide-hero { grid-template-columns:1fr; } .ready-panel { max-width:580px; } }
  @media(max-width:480px) { .guide-steps li { grid-template-columns:48px minmax(0,1fr); gap:.9rem; } .guide-steps .count { font-size:1.7rem; } .actions a { width:100%; } }
</style>
