<script lang="ts">
  import { base } from '$app/paths';
  import { onMount } from 'svelte';
  import { lang } from '$lib/i18n';
  import { publicContent } from '$lib/publicContent';
  import facts from '$lib/generated/firmwareFacts.json';

  const languages = ['en', 'lv', 'ru', 'nl', 'de'] as const;
  const limits = facts.limits;
  const lightPoints = [0, 1, 2, 3, 4, 5, 6, 7];
  let ready = false;
  $: copy = publicContent[$lang];
  onMount(() => { ready = true; });
</script>

<svelte:head>
  <title>{copy.metaTitle}</title>
  <meta name="description" content={copy.metaDescription} />
  <meta property="og:title" content={copy.metaTitle} />
  <meta property="og:description" content={copy.metaDescription} />
  <meta name="twitter:title" content={copy.metaTitle} />
  <meta name="twitter:description" content={copy.metaDescription} />
</svelte:head>

<div class="public-landing" lang={$lang}>
  <header class="container public-header">
    <a class="wordmark" href={`${base}/`} aria-label="LumiGrid LED Node">
      <strong>LUMIGRID</strong><span>LED NODE</span>
    </a>
    <nav aria-label="LumiGrid">
      <a href="#capabilities">{copy.nav[0]}</a>
      <a href="#outputs">{copy.nav[1]}</a>
      <a href={`${base}/docs`}>{copy.nav[2]}</a>
    </nav>
    <div class="header-actions">
      <label class="language"><span>{copy.language}</span>
        <select bind:value={$lang} disabled={!ready}>
          {#each languages as code}<option value={code}>{code.toUpperCase()}</option>{/each}
        </select>
      </label>
      <a class="header-app" href={`${base}/dashboard`}>{copy.nav[3]} <span aria-hidden="true">↗</span></a>
    </div>
  </header>

  <section class="container hero section" aria-labelledby="hero-title">
    <div class="hero-copy">
      <p class="eyebrow">{copy.kicker}</p>
      <h1 id="hero-title" data-testid="hero-title">{copy.title}</h1>
      <p class="lead">{copy.intro}</p>
      <div class="actions">
        <a class="btn primary" href={`${base}/docs/GETTING_STARTED`}>{copy.start} <span aria-hidden="true">↗</span></a>
        <a class="btn" href={`${base}/dashboard`}>{copy.explore} <span aria-hidden="true">→</span></a>
      </div>
      <p class="helper">{copy.dashboardNote}</p>
    </div>

    <aside class="light-card card" aria-label={copy.visualLabel}>
      <div class="light-card__head"><span class="eyebrow">{copy.visualEyebrow}</span><span class="light-card__brand">LED NODE</span></div>
      <h2>{copy.visualTitle}</h2>
      <div class="light-stage" aria-hidden="true">
        <div class="light-stage__strip one"></div>
        <div class="light-stage__strip two"></div>
        <div class="light-stage__strip three"></div>
        <div class="light-stage__pool"></div>
      </div>
      <div class="light-card__summary">
        <div class="light-card__number"><strong>{limits.addressable + limits.pwm}</strong><span>{copy.totalOutputs}</span></div>
        <div class="light-card__types"><span>{limits.addressable} {copy.pixelTag}</span><span>{limits.pwm} {copy.dimmerTag}</span></div>
      </div>
      <p class="visual-caption">{copy.visualCaption}</p>
    </aside>
  </section>

  <section id="capabilities" class="container section" aria-labelledby="capabilities-title">
    <div class="section-intro"><p class="eyebrow">{copy.featuresEyebrow}</p><h2 id="capabilities-title">{copy.featuresTitle}</h2><p>{copy.featuresIntro}</p></div>
    <div class="capability-stats">
      <div><strong>{limits.addressable + limits.pwm}</strong><span>{copy.totalOutputs}</span></div>
      <div><strong>{limits.scenes}</strong><span>{copy.scenesStat}</span></div>
      <div><strong>{limits.effects}</strong><span>{copy.effectsStat}</span></div>
    </div>
    <div class="feature-grid">
      {#each copy.features as feature, index}
        <article class="card feature">
          <span class="feature__number">{String(index + 1).padStart(2, '0')}</span>
          <h3>{feature[0]}</h3><p>{feature[1]}</p>
          <a href={`${base}/docs/${feature[2]}`}>{copy.read} <span aria-hidden="true">→</span></a>
        </article>
      {/each}
    </div>
  </section>

  <section id="outputs" class="container section" aria-labelledby="outputs-title">
    <div class="section-intro"><p class="eyebrow">{copy.outputsEyebrow}</p><h2 id="outputs-title">{copy.outputsTitle}</h2><p>{copy.outputsIntro}</p></div>
    <div class="output-grid">
      <article class="card output-card">
        <div class="output-card__heading"><strong>{limits.addressable}</strong><span>{copy.pixelTag}</span></div>
        <div class="pixel-line" aria-hidden="true">{#each lightPoints as point (point)}<span></span>{/each}</div>
        <h3>{copy.pixelTitle}</h3><p>{copy.pixelText}</p>
      </article>
      <article class="card output-card">
        <div class="output-card__heading"><strong>{limits.pwm}</strong><span>{copy.dimmerTag}</span></div>
        <div class="dimmer-line" aria-hidden="true">{#each lightPoints as point (point)}<span></span>{/each}</div>
        <h3>{copy.dimmerTitle}</h3><p>{copy.dimmerText}</p>
      </article>
    </div>
    <p class="output-note">{copy.outputsNote}</p>
  </section>

  <section id="setup" class="container section" aria-labelledby="setup-title">
    <div class="section-intro"><p class="eyebrow">{copy.setupEyebrow}</p><h2 id="setup-title">{copy.setupTitle}</h2></div>
    <ol class="steps">
      {#each copy.steps as step, index}
        <li><span class="step-count">{String(index + 1).padStart(2, '0')}</span><h3>{step[0]}</h3><p>{step[1]}</p></li>
      {/each}
    </ol>
  </section>

  <section class="container section details" aria-label={copy.localTitle}>
    <article><p class="eyebrow">LUMIGRID LED NODE</p><h2>{copy.localTitle}</h2><p>{copy.localText}</p><p>{copy.localDetail}</p></article>
    <article class="details__essential"><h3>{copy.essentialsTitle}</h3><p>{copy.essentialsText}</p>
      <a href={`${base}/docs/LED_NODE`}>{copy.read} <span aria-hidden="true">→</span></a>
    </article>
  </section>

  <section class="container section" aria-labelledby="cta-title">
    <div class="card final-cta">
      <div><h2 id="cta-title">{copy.ctaTitle}</h2><p>{copy.ctaText}</p></div>
      <div class="actions">
        <a class="btn primary" href={`${base}/docs/GETTING_STARTED`}>{copy.docs} <span aria-hidden="true">↗</span></a>
        <a class="btn" href={`${base}/dashboard`}>{copy.app} <span aria-hidden="true">→</span></a>
      </div>
    </div>
  </section>

  <footer class="container public-footer">
    <span>{copy.footer}</span>
    <a href={`${base}/docs`}>{copy.technical} <span aria-hidden="true">↗</span></a>
  </footer>
</div>

<style>
  .public-landing { position:relative; overflow-x:clip; }
  .public-header { display:flex; align-items:center; flex-wrap:wrap; justify-content:space-between; gap:1.15rem; padding-block:1.3rem; border-bottom:1px solid var(--glass-border); }
  .wordmark { display:flex; flex-direction:column; color:var(--ink); text-decoration:none; line-height:1.15; min-width:max-content; }
  .wordmark strong { font-size:1.15rem; font-weight:800; letter-spacing:.11em; }
  .wordmark span { color:var(--accent-primary); font-size:.66rem; letter-spacing:.3em; font-weight:750; margin-top:.28rem; }
  nav,.header-actions,.language,.actions { display:flex; align-items:center; flex-wrap:wrap; }
  nav { gap:clamp(.75rem,2vw,1.5rem); }
  nav a,.header-app,.public-footer a { color:var(--ink); text-decoration:none; font-size:.9rem; font-weight:550; }
  nav a:hover,.header-app:hover,.public-footer a:hover { color:var(--accent-primary); }
  .header-actions { gap:1rem; }
  .header-app { border-bottom:1px solid var(--accent-primary); padding-bottom:.14rem; }
  .language { gap:.4rem; font-size:.8rem; color:var(--muted); }
  .language select { color:var(--ink); background:var(--bg-1); border:1px solid var(--glass-border); border-radius:.6rem; padding:.55rem .7rem; min-height:44px; }
  .hero { display:grid; grid-template-columns:minmax(0,1.06fr) minmax(0,.94fr); gap:clamp(2rem,4vw,4rem); align-items:center; padding-block:clamp(3.5rem,8vw,7rem); }
  .eyebrow { color:var(--accent-primary); font-size:.74rem; font-weight:700; letter-spacing:.14em; text-transform:uppercase; line-height:1.45; }
  h1 { font-size:clamp(2.65rem,5.5vw,5.2rem); letter-spacing:-.055em; line-height:1.05; max-width:14ch; margin:1rem 0 1.35rem; color:var(--ink); background:none; -webkit-background-clip:initial; background-clip:initial; }
  h2 { font-size:clamp(1.75rem,3.2vw,3rem); line-height:1.16; letter-spacing:-.035em; margin:.55rem 0 1rem; }
  h3 { font-size:1.2rem; line-height:1.35; margin:.8rem 0; }
  p { color:var(--muted); line-height:1.7; }
  .lead { font-size:clamp(1.05rem,1.7vw,1.25rem); line-height:1.65; max-width:58ch; }
  .actions { gap:.8rem; margin-top:1.7rem; align-items:stretch; }
  .actions a { justify-content:center; text-align:center; min-height:44px; }
  .helper { font-size:.82rem; margin-top:1.1rem; }
  .light-card { padding:clamp(1.2rem,2.2vw,1.8rem); background:linear-gradient(155deg,rgba(32,30,37,.96),rgba(10,10,12,.98)); color:#f5f5f7; overflow:hidden; }
  .light-card__head { display:flex; justify-content:space-between; align-items:center; gap:1rem; }
  .light-card__brand { font-size:.66rem; font-weight:800; letter-spacing:.16em; color:#f5f5f7a8; }
  .light-card h2 { color:#f5f5f7; font-size:clamp(1.25rem,2vw,1.75rem); max-width:22ch; margin:1rem 0; }
  .light-stage { position:relative; isolation:isolate; min-height:230px; height:clamp(230px,24vw,300px); border-radius:14px; overflow:hidden; background:radial-gradient(ellipse at 55% 95%,#25202b 0%,#0d0d13 54%,#07070a 100%); border:1px solid #ffffff15; }
  .light-stage__strip { position:absolute; z-index:2; left:12%; width:76%; height:7px; border-radius:10px; transform:skewY(-8deg); box-shadow:0 0 16px currentColor,0 0 34px currentColor; }
  .light-stage__strip.one { top:25%; color:#ff2d55; background:linear-gradient(90deg,#ff2d55,#ff9500 36%,#ff2d55 72%,#ff9500); }
  .light-stage__strip.two { top:43%; left:21%; width:67%; color:#00d4ff; background:linear-gradient(90deg,#00d4ff,#8c75ff,#ff2d55); transform:skewY(9deg); }
  .light-stage__strip.three { top:61%; left:10%; width:73%; color:#ff9500; background:linear-gradient(90deg,#ff9500,#ff3b30,#ff2d55); }
  .light-stage__pool { position:absolute; bottom:-20%; left:10%; right:10%; height:65%; background:linear-gradient(90deg,#ff2d5555,#ff950033,#00d4ff55); filter:blur(32px); }
  .light-card__summary { display:flex; flex-wrap:wrap; align-items:center; gap:1rem; padding-top:1.2rem; }
  .light-card__number { display:flex; align-items:center; gap:.7rem; flex:1 1 160px; min-width:0; }
  .light-card__number strong { color:#fff; font-size:3.6rem; line-height:1; font-weight:650; }
  .light-card__number span { color:#f5f5f7cf; font-size:.85rem; max-width:15ch; line-height:1.35; }
  .light-card__types { display:grid; gap:.4rem; font-size:.79rem; color:#f5f5f7c9; }
  .light-card__types span::before { content:""; display:inline-block; width:.5rem; height:.5rem; border-radius:50%; background:var(--accent-primary); margin-right:.5rem; }
  .light-card__types span:last-child::before { background:var(--rf-amber); }
  .visual-caption { color:#ffffff99; font-size:.72rem; margin:1.15rem 0 0; }
  .section-intro { max-width:760px; margin-bottom:2.2rem; }
  .section-intro p:last-child { max-width:65ch; font-size:1.04rem; }
  .capability-stats { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:1px; border-block:1px solid var(--glass-border); margin:1.5rem 0 2.4rem; }
  .capability-stats div { padding:1.4rem clamp(.7rem,2vw,2rem); display:flex; flex-direction:column; gap:.25rem; }
  .capability-stats div + div { border-left:1px solid var(--glass-border); }
  .capability-stats strong { font-size:clamp(2rem,4vw,3.4rem); letter-spacing:-.04em; line-height:1.1; font-weight:650; color:var(--ink); }
  .capability-stats span { font-size:.88rem; color:var(--muted); line-height:1.4; }
  .feature-grid { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:1rem; }
  .feature { display:flex; flex-direction:column; padding:clamp(1.3rem,2vw,1.8rem); min-height:240px; }
  .feature__number { color:var(--accent-primary); font-size:.8rem; font-weight:700; letter-spacing:.1em; }
  .feature p { flex:1; margin:.15rem 0 1.3rem; }
  .feature a,.details__essential a { align-self:flex-start; color:var(--ink); text-decoration-thickness:1px; text-underline-offset:4px; font-size:.91rem; font-weight:600; }
  .feature a:hover,.details__essential a:hover { color:var(--accent-primary); }
  .output-grid { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:1rem; }
  .output-card { display:flex; flex-direction:column; padding:clamp(1.4rem,2.5vw,2rem); min-width:0; }
  .output-card__heading { display:flex; justify-content:space-between; align-items:center; gap:1rem; }
  .output-card__heading strong { font-size:3.7rem; line-height:1; font-weight:600; color:var(--ink); }
  .output-card__heading span { color:var(--muted); font-size:.87rem; text-align:right; }
  .output-card h3 { font-size:1.35rem; margin:1.6rem 0 .2rem; }
  .output-card p { margin:.3rem 0 0; }
  .pixel-line,.dimmer-line { display:grid; grid-template-columns:repeat(8,minmax(0,1fr)); gap:8px; align-items:end; padding:2rem .4rem 1rem; min-height:112px; }
  .pixel-line span,.dimmer-line span { height:48px; border-radius:9px; background:#ff2d55; box-shadow:0 0 14px #ff2d5550; }
  .pixel-line span:nth-child(2),.pixel-line span:nth-child(6) { background:#ff9500; box-shadow:0 0 14px #ff950055; }
  .pixel-line span:nth-child(3),.pixel-line span:nth-child(7) { background:#00d4ff; box-shadow:0 0 14px #00d4ff55; }
  .pixel-line span:nth-child(4),.pixel-line span:nth-child(8) { background:#a77cfa; box-shadow:0 0 14px #a77cfa55; }
  .dimmer-line span { background:#ffb86a; box-shadow:0 0 14px #ff950030; }
  .dimmer-line span:nth-child(1) { opacity:.18; } .dimmer-line span:nth-child(2) { opacity:.3; }
  .dimmer-line span:nth-child(3) { opacity:.42; } .dimmer-line span:nth-child(4) { opacity:.56; }
  .dimmer-line span:nth-child(5) { opacity:.7; } .dimmer-line span:nth-child(6) { opacity:.82; }
  .dimmer-line span:nth-child(7) { opacity:.92; }
  .output-note { max-width:80ch; margin:1rem 0 0; font-size:.82rem; }
  .steps { padding:0; margin:0; list-style:none; display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:clamp(1rem,3vw,3rem); }
  .steps li { padding-top:1.5rem; border-top:1px solid var(--glass-border); }
  .step-count { color:var(--accent-primary); font-size:1.5rem; font-weight:650; letter-spacing:-.03em; }
  .steps h3 { margin:.9rem 0 .35rem; }
  .steps p { margin:.35rem 0; }
  .details { display:grid; grid-template-columns:1.1fr .9fr; gap:clamp(2rem,5vw,5rem); border-top:1px solid var(--glass-border); }
  .details h2 { max-width:18ch; }
  .details__essential { padding:clamp(1.3rem,3vw,2rem); border-left:2px solid var(--accent-primary); align-self:center; }
  .details__essential h3 { margin-top:0; }
  .final-cta { display:flex; align-items:center; justify-content:space-between; flex-wrap:wrap; gap:1.5rem; padding:clamp(1.5rem,4vw,3.25rem); }
  .final-cta > div:first-child { max-width:640px; }
  .final-cta .actions { margin:0; }
  .final-cta h2 { margin-top:0; }
  .public-footer { display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:1rem; border-top:1px solid var(--glass-border); padding-block:1.6rem 2.7rem; font-size:.83rem; color:var(--muted); }
  .public-footer a { font-size:.83rem; }
  a:focus-visible,select:focus-visible { outline:2px solid var(--accent-primary); outline-offset:4px; }
  @media (max-width:1040px) { .header-app { display:none; } .feature-grid { grid-template-columns:repeat(2,minmax(0,1fr)); } }
  @media (max-width:820px) { .hero,.details { grid-template-columns:1fr; } .hero { padding-block:3rem; } .hero-copy { max-width:680px; } h1 { max-width:17ch; } .light-card { max-width:640px; width:100%; } .details__essential { border-left:0; border-top:2px solid var(--accent-primary); padding-inline:0; } }
  @media (max-width:600px) {
    .public-header nav { width:100%; order:3; overflow-x:auto; white-space:nowrap; padding-bottom:.25rem; }
    .header-actions { margin-left:auto; } .language span { display:none; }
    .hero { padding-top:2.5rem; } h1 { font-size:clamp(2.4rem,10vw,3.7rem); }
    .feature-grid,.output-grid,.steps { grid-template-columns:1fr; }
    .capability-stats div { padding:1.2rem .45rem; }
    .capability-stats strong { font-size:2.15rem; }
    .capability-stats span { font-size:.73rem; }
    .feature { min-height:0; } .light-card__number strong { font-size:3rem; }
    .final-cta .actions { width:100%; } .final-cta .btn { flex:1 1 100%; }
  }
</style>
