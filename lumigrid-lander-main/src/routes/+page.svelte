<script lang="ts">
  import { base } from '$app/paths';
  import { onMount } from 'svelte';
  import { lang } from '$lib/i18n';
  import { publicContent } from '$lib/publicContent';
  import facts from '$lib/generated/firmwareFacts.json';
  $: copy = publicContent[$lang];
  const languages = ['en', 'lv', 'ru', 'nl', 'de'] as const;
  const limits = facts.limits;
  let ready = false;
  onMount(() => { ready = true; });
</script>

<svelte:head>
  <title>{copy.kicker}</title>
  <meta name="description" content={copy.intro} />
  <meta property="og:title" content="LumiGrid — local lighting control" />
  <meta property="og:description" content={copy.intro} />
</svelte:head>

<div lang={$lang}>
  <header class="container public-header">
    <a class="wordmark" href={`${base}/`}>LUMIGRID<span>LED NODE</span></a>
    <nav aria-label={copy.kicker}>
      <a href="#capabilities">{copy.nav[0]}</a>
      <a href="#setup">{copy.nav[1]}</a>
      <a href={`${base}/docs`}>{copy.nav[2]}</a>
      <a href={`${base}/dashboard`}>{copy.nav[3]}</a>
    </nav>
    <label class="language">{copy.language}
      <select bind:value={$lang} disabled={!ready}>
        {#each languages as code}<option value={code}>{code.toUpperCase()}</option>{/each}
      </select>
    </label>
  </header>

  <section class="container section hero">
    <div>
      <p class="eyebrow">{copy.kicker}</p>
      <h1 data-testid="hero-title">{copy.title}</h1>
      <p class="lead">{copy.intro}</p>
      <div class="actions">
        <a class="btn primary" href={`${base}/docs/GETTING_STARTED`}>{copy.start}</a>
        <a class="btn" href={`${base}/dashboard`}>{copy.explore}</a>
      </div>
    </div>
    <aside class="card topology" aria-label={copy.topology}>
      <p class="eyebrow">ESP32 / LED NODE</p>
      <h2>{copy.topology}</h2>
      <div class="stats">
        <div><strong>{limits.addressable}</strong><span>{copy.addressable}</span></div>
        <div><strong>{limits.pwm}</strong><span>{copy.pwm}</span></div>
        <div><strong>{limits.scenes}</strong><span>{copy.scenes}</span></div>
        <div><strong>{limits.effects}</strong><span>{copy.effects}</span></div>
      </div>
      <p>{copy.hardware}</p>
      <p class="muted">{copy.capacity}</p>
    </aside>
  </section>

  <section id="capabilities" class="container section">
    <h2>{copy.featuresTitle}</h2>
    <div class="feature-grid">
      {#each copy.features as feature, index}
        <article class="card">
          <span class="number">0{index + 1}</span>
          <h3>{feature[0]}</h3><p>{feature[1]}</p>
          <a href={`${base}/docs/${feature[2]}`}>{copy.read} →</a>
        </article>
      {/each}
    </div>
  </section>

  <section id="setup" class="container section">
    <h2>{copy.setupTitle}</h2>
    <ol class="steps">
      {#each copy.steps as step}
        <li><h3>{step[0]}</h3><p>{step[1]}</p></li>
      {/each}
    </ol>
    <a href={`${base}/docs/GETTING_STARTED`}>{copy.start} →</a>
  </section>

  <section class="container section notes">
    <article><h2>{copy.localTitle}</h2><p>{copy.localText}</p></article>
    <article><h2>{copy.limitsTitle}</h2><p>{copy.limitsText}</p>
      <a href={`${base}/docs/SOURCE_OF_TRUTH`}>{copy.audit} →</a>
    </article>
  </section>

  <section class="container section card docs-cta">
    <h2>{copy.docsTitle}</h2><p>{copy.docsText}</p>
    <div class="actions">
      <a class="btn primary" href={`${base}/docs`}>{copy.docs}</a>
      <a class="btn" href={`${base}/flasher`}>{copy.firmware}</a>
    </div>
  </section>
  <footer class="container section">{copy.footer}</footer>
</div>

<style>
  .public-header { display:flex; flex-wrap:wrap; align-items:center; justify-content:space-between; gap:1.5rem; padding-block:1.5rem; border-bottom:1px solid #ffffff26; }
  .wordmark { font-size:1.15rem; font-weight:800; letter-spacing:.12em; color:var(--ink); text-decoration:none; }
  .wordmark span { display:block; font-size:.6rem; letter-spacing:.3em; color:var(--muted); }
  nav { display:flex; flex-wrap:wrap; gap:1.25rem; }
  nav a { color:var(--ink); text-decoration:none; }
  .language { display:flex; align-items:center; gap:.6rem; font-size:.8rem; }
  select { color:var(--ink); background:var(--bg); border:1px solid #ffffff55; border-radius:.4rem; padding:.55rem; }
  .hero { display:grid; grid-template-columns:1.2fr 1fr; gap:3rem; align-items:center; padding-block:5rem; }
  h1 { font-size:clamp(2.7rem,5vw,5rem); line-height:1.04; letter-spacing:-.045em; max-width:12ch; margin:1rem 0 1.5rem; }
  h2 { font-size:clamp(1.5rem,2.6vw,2.3rem); line-height:1.2; margin:0 0 1.5rem; }
  h3 { font-size:1.2rem; line-height:1.35; margin:.7rem 0; }
  p { color:var(--muted); line-height:1.7; max-width:68ch; }
  .lead { font-size:1.15rem; }
  .eyebrow,.number { text-transform:uppercase; letter-spacing:.12em; font-size:.75rem; color:var(--accent-primary,var(--ink)); }
  .actions { display:flex; flex-wrap:wrap; gap:.8rem; margin-top:1.8rem; }
  .topology { padding:2rem; }
  .topology h2 { font-size:1.5rem; }
  .stats { display:grid; grid-template-columns:1fr 1fr; gap:1.8rem; margin:2rem 0; }
  .stats strong { display:block; font-size:3rem; line-height:1.1; font-weight:500; }
  .stats span { font-size:.85rem; color:var(--muted); }
  .muted { font-size:.8rem; }
  .feature-grid { display:grid; grid-template-columns:repeat(3,1fr); gap:1rem; }
  .feature-grid article { display:flex; flex-direction:column; padding:1.5rem; min-width:0; content-visibility:visible; }
  .feature-grid p { flex:1; }
  .feature-grid a,.notes a { margin-top:1rem; }
  .steps { display:grid; grid-template-columns:repeat(3,1fr); gap:3rem; padding-left:1.5rem; margin-block:2rem; }
  .steps li { padding-left:.5rem; }
  .notes { display:grid; grid-template-columns:1fr 1fr; gap:3rem; border-block:1px solid #ffffff26; }
  .notes h2 { font-size:1.5rem; }
  .docs-cta { padding:2.5rem; margin-top:3rem; }
  footer { color:var(--muted); font-size:.8rem; }
  a:focus-visible,select:focus-visible { outline:2px solid currentColor; outline-offset:5px; }
  @media(max-width:900px) { .hero { grid-template-columns:1fr; padding-block:3rem; } .feature-grid { grid-template-columns:repeat(2,1fr); } h1 { max-width:16ch; } }
  @media(max-width:600px) { .feature-grid,.steps,.notes { grid-template-columns:1fr; } .steps { gap:1rem; } nav { gap:1rem; font-size:.85rem; } .topology,.docs-cta { padding:1.5rem; } }
</style>
