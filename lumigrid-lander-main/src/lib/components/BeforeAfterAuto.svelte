<script>
  const pairs = [
    {
      name: 'Retail frontage',
      before: {
        caption: 'Before: harsh tubes and glare',
        background: 'var(--glass-bg)'
      },
      after: {
        caption: 'After: diffused halo with focus zones',
        background: `var(--glass-bg), radial-gradient(140% 140% at 20% 70%, var(--cya) 0%, transparent 55%)`
      }
    },
    {
      name: 'Logo letter set',
      before: {
        caption: 'Before: uneven colour mix',
        background: 'var(--glass-bg)'
      },
      after: {
        caption: 'After: tuned edge glow',
        background: `var(--glass-bg), radial-gradient(140% 140% at 70% 65%, var(--amb) 0%, transparent 55%)`
      }
    },
    {
      name: 'Outdoor canopy',
      before: {
        caption: 'Before: hotspots on acrylic',
        background: 'var(--glass-bg)'
      },
      after: {
        caption: 'After: even wash with amber cue',
        background: `var(--glass-bg), radial-gradient(150% 150% at 40% 70%, var(--mag) 0%, transparent 55%)`
      }
    }
  ];

  let active = null;
  let pct = 50;
  let drag = false;

  function updatePct(event, element = event.currentTarget) {
    const rect = element.getBoundingClientRect();
    pct = Math.max(0, Math.min(100, 100 * (event.clientX - rect.left) / rect.width));
  }
</script>

<section class="container section" data-hue="220">
  <h2 id="before-after">Before / After</h2>
  <div class="grid grid-2">
    {#each pairs as p}
      <div class="card" on:pointerenter={() => active = p} on:pointerleave={() => active = null}>
        <div class="preview" role="img" aria-label={`${p.name} lighting comparison`}>
          <div class="frame" style={`background:${p.before.background}`}>
            <span>{p.before.caption}</span>
          </div>
          <div class="frame after" style={`background:${p.after.background}; clip-path:inset(0 ${100-pct}% 0 0)`}>
            <span>{p.after.caption}</span>
          </div>
        </div>
        <p class="label">{p.name}</p>
      </div>
    {/each}
  </div>

  {#if active}
    <div class="lightbox">
      <div
        class="preview large"
        role="img"
        aria-label={`${active.name} detailed lighting comparison`}
        on:pointerdown={(e) => { drag = true; updatePct(e, e.currentTarget); }}
        on:pointerup={() => drag = false}
        on:pointerleave={() => drag = false}
        on:pointermove={(e) => { if (!drag) return; updatePct(e, e.currentTarget); }}
      >
        <div class="frame" style={`background:${active.before.background}`}>
          <span>{active.before.caption}</span>
        </div>
        <div class="frame after" style={`background:${active.after.background}; clip-path:inset(0 ${100-pct}% 0 0)`}>
          <span>{active.after.caption}</span>
        </div>
        <button class="slider" style={`left:${pct}%`} on:pointerdown|stopPropagation={(e) => { drag = true; updatePct(e, e.currentTarget.parentElement); }} aria-label="Slide to reveal after image"></button>
      </div>
      <button on:click={() => active = null} class="btn">Close</button>
    </div>
  {/if}
</section>

<style>
  .preview { position: relative; width: 100%; height: 220px; border-radius: .8rem; overflow: hidden; }
  .preview.large { height: min(70vh, 520px); width: min(90vw, 960px); }
  .frame {
    position: absolute;
    inset: 0;
    display: flex;
    align-items: flex-end;
    padding: 1rem;
    background-size: cover;
    background-position: center;
    color: var(--ink);
    text-shadow: var(--shadow);
  }
  .frame span { font-weight: 600; max-width: 18ch; }
  .after { pointer-events: none; transition: clip-path .25s var(--ease-out); }
  .label { margin-top: .6rem; font-weight: 600; }
  .slider {
    position: absolute;
    top: 0;
    bottom: 0;
    width: 44px;
    background: var(--glass-border);
    cursor: ew-resize;
    transform: translateX(-50%);
    border: none;
    padding: 0;
    backdrop-filter: blur(2px);
  }
  .lightbox { position: fixed; inset: 0; background: var(--glass-bg); backdrop-filter: blur(20px); z-index: 100; display: grid; place-content: center; gap: 1rem; padding: 1rem; }
</style>
