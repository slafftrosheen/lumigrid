<script>
  import { tokens } from '$lib/tokens';
  
  let copiedToken = '';
  let selectedHalo = 2;
  let animationSpeed = 'normal';
  let reducedMotion = false;
  
  const colorCategories = [
    {
      name: 'Brand Colors',
      colors: [
        { name: 'Primary (Apple Blue)', value: tokens.colors.brand.primary, css: '--accent-primary' },
        { name: 'LED Cyan', value: tokens.colors.brand.ledCyan, css: '--cya' },
        { name: 'LED Amber', value: tokens.colors.brand.ledAmber, css: '--amb' },
        { name: 'LED Magenta', value: tokens.colors.brand.ledMagenta, css: '--mag' },
        { name: 'LED Cherry', value: tokens.colors.brand.ledCherry, css: '--cherry' },
      ]
    },
    {
      name: 'Neutral Colors',
      colors: [
        { name: 'Background Base', value: tokens.colors.neutral.bg.base, css: '--bg' },
        { name: 'Background Primary', value: tokens.colors.neutral.bg.primary, css: '--bg-0' },
        { name: 'Background Secondary', value: tokens.colors.neutral.bg.secondary, css: '--bg-1' },
        { name: 'Text Primary', value: tokens.colors.neutral.text.primary, css: '--ink' },
        { name: 'Text Secondary', value: tokens.colors.neutral.text.secondary, css: '--muted' },
      ]
    }
  ];
  
  const spacingTokens = Object.entries(tokens.spacing).map(([key, value]) => ({
    name: key,
    value: value,
    css: `--spacing-mobile-${key}`
  }));
  
  const radiusTokens = Object.entries(tokens.radius).map(([key, value]) => ({
    name: key,
    value: value
  }));
  
  function copyToClipboard(text, tokenName) {
    navigator.clipboard.writeText(text).then(() => {
      copiedToken = tokenName;
      setTimeout(() => copiedToken = '', 2000);
    });
  }
  
  function toggleReducedMotion() {
    reducedMotion = !reducedMotion;
    if (reducedMotion) {
      document.documentElement.style.setProperty('--dur-fast', '0ms');
      document.documentElement.style.setProperty('--dur-med', '0ms');
      document.documentElement.style.setProperty('--dur-slow', '0ms');
    } else {
      document.documentElement.style.setProperty('--dur-fast', '150ms');
      document.documentElement.style.setProperty('--dur-med', '300ms');
      document.documentElement.style.setProperty('--dur-slow', '500ms');
    }
  }
</script>

<div class="container" style="padding-top: 2rem; padding-bottom: 4rem;">
  <section class="hero center">
    <h1>LumiGrid Style System</h1>
    <p class="lead">Interactive design token showcase and accessibility demos</p>
    <nav class="pills" aria-label="Page sections">
      <a href="#colors" class="pill">Colors</a>
      <a href="#spacing" class="pill">Spacing</a>
      <a href="#glass" class="pill">Glass Effects</a>
      <a href="#motion" class="pill">Motion</a>
      <a href="#accessibility" class="pill">Accessibility</a>
    </nav>
  </section>

  <!-- Color Palette Section -->
  <section id="colors" class="section">
    <h2 class="under">Color Palette</h2>
    {#each colorCategories as category}
      <div class="card" data-halo="1" style="margin-bottom: 2rem;">
        <h3>{category.name}</h3>
        <div class="color-grid">
          {#each category.colors as color}
            <div class="color-swatch-card">
              <div 
                class="color-swatch" 
                style="background-color: {color.value};"
                role="img"
                aria-label="{color.name} color swatch, value {color.value}"
              ></div>
              <div class="color-info">
                <strong>{color.name}</strong>
                <code class="color-code">{color.value}</code>
                <code class="css-var">var({color.css})</code>
                <div class="color-actions">
                  <button 
                    class="btn ghost" 
                    style="font-size: 0.875rem; padding: 0.4rem 0.7rem;"
                    on:click={() => copyToClipboard(color.value, color.name)}
                  >
                    {copiedToken === color.name ? '✓ Copied' : 'Copy HEX'}
                  </button>
                  <button 
                    class="btn ghost" 
                    style="font-size: 0.875rem; padding: 0.4rem 0.7rem;"
                    on:click={() => copyToClipboard(`var(${color.css})`, `${color.name}-css`)}
                  >
                    {copiedToken === `${color.name}-css` ? '✓ Copied' : 'Copy CSS'}
                  </button>
                </div>
              </div>
            </div>
          {/each}
        </div>
      </div>
    {/each}
  </section>

  <!-- Spacing Section -->
  <section id="spacing" class="section">
    <h2 class="under">Spacing Scale</h2>
    <div class="card" data-halo="1">
      <h3>Spacing Tokens</h3>
      <p style="color: var(--muted);">Consistent spacing creates visual rhythm and hierarchy</p>
      <div class="spacing-demo">
        {#each spacingTokens as space}
          <div class="spacing-item">
            <div class="spacing-visual" style="width: {space.value};"></div>
            <div class="spacing-label">
              <strong>{space.name}</strong>
              <code>{space.value}</code>
            </div>
          </div>
        {/each}
      </div>
    </div>
  </section>

  <!-- Border Radius Section -->
  <section class="section">
    <h2 class="under">Border Radius</h2>
    <div class="card" data-halo="1">
      <h3>Radius Tokens</h3>
      <div class="radius-demo">
        {#each radiusTokens as radius}
          <div class="radius-item">
            <div 
              class="radius-box" 
              style="border-radius: {radius.value};"
              role="img"
              aria-label="Border radius {radius.name}, value {radius.value}"
            ></div>
            <div class="radius-label">
              <strong>{radius.name}</strong>
              <code>{radius.value}</code>
            </div>
          </div>
        {/each}
      </div>
    </div>
  </section>

  <!-- Glass Effects Section -->
  <section id="glass" class="section">
    <h2 class="under">Glassmorphism & Halo Effects</h2>
    <div class="card" data-halo="1" style="margin-bottom: 2rem;">
      <h3>Halo Intensity Levels</h3>
      <p style="color: var(--muted);">Adjust the halo intensity using the data-halo attribute (0-4)</p>
      
      <div class="controls" style="margin: 1.5rem 0;">
        <label for="halo-slider" style="display: flex; flex-direction: column; gap: 0.5rem;">
          <span>Halo Level: <strong>{selectedHalo}</strong></span>
          <input 
            id="halo-slider"
            type="range" 
            min="0" 
            max="4" 
            bind:value={selectedHalo}
            style="width: 100%; max-width: 300px;"
          />
        </label>
      </div>

      <div class="halo-demo-grid">
        {#each [0, 1, 2, 3, 4] as level}
          <div class="card" data-halo={level.toString()} style="padding: 1.5rem; text-align: center;">
            <strong>Level {level}</strong>
            <p style="font-size: 0.875rem; color: var(--muted); margin: 0.5rem 0 0;">
              {level === 0 ? 'None' : level === 1 ? 'Subtle' : level === 2 ? 'Soft' : level === 3 ? 'Strong' : 'Vivid'}
            </p>
          </div>
        {/each}
      </div>
    </div>

    <div class="card" data-halo="2">
      <h3>Animated Gradient Frame</h3>
      <p style="color: var(--muted); margin-bottom: 1.5rem;">
        The grad-frame class creates a rotating gradient border effect
      </p>
      <div class="grad-frame" style="padding: 2rem; text-align: center;">
        <h4 style="margin: 0;">Rotating Border</h4>
        <p style="margin: 0.5rem 0 0; color: var(--muted);">
          This frame has an animated conic gradient
        </p>
      </div>
    </div>
  </section>

  <!-- Motion System Section -->
  <section id="motion" class="section">
    <h2 class="under">Motion & Animation System</h2>
    <div class="card" data-halo="1">
      <h3>Transition Tokens</h3>
      <p style="color: var(--muted);">All animations respect prefers-reduced-motion</p>
      
      <div class="motion-demo">
        <div class="motion-item">
          <div class="motion-box fast">Fast (150ms)</div>
          <code>--dur-fast</code>
        </div>
        <div class="motion-item">
          <div class="motion-box normal">Normal (300ms)</div>
          <code>--dur-med</code>
        </div>
        <div class="motion-item">
          <div class="motion-box slow">Slow (500ms)</div>
          <code>--dur-slow</code>
        </div>
      </div>

      <div class="controls" style="margin-top: 2rem;">
        <button class="btn {reducedMotion ? 'primary' : 'outline'}" on:click={toggleReducedMotion}>
          {reducedMotion ? 'Motion Disabled' : 'Motion Enabled'}
        </button>
        <p style="margin: 0.5rem 0 0; font-size: 0.875rem; color: var(--muted);">
          Toggle to simulate prefers-reduced-motion
        </p>
      </div>
    </div>
  </section>

  <!-- Accessibility Section -->
  <section id="accessibility" class="section">
    <h2 class="under">Accessibility Features</h2>
    
    <div class="grid two">
      <div class="card" data-halo="2">
        <h3>Focus States</h3>
        <p style="color: var(--muted);">All interactive elements have visible focus indicators</p>
        <div style="display: flex; flex-direction: column; gap: 1rem; margin-top: 1rem;">
          <button class="btn primary">Primary Button</button>
          <button class="btn outline">Outline Button</button>
          <button class="btn ghost">Ghost Button</button>
          <input type="text" placeholder="Focus me with Tab" />
        </div>
        <p style="margin-top: 1rem; font-size: 0.875rem; color: var(--muted);">
          Try using Tab key to navigate through these elements
        </p>
      </div>

      <div class="card" data-halo="2">
        <h3>Touch Targets</h3>
        <p style="color: var(--muted);">All interactive elements meet 44×44px minimum</p>
        <div class="chips" style="margin-top: 1rem;">
          <button class="chip">Chip 1</button>
          <button class="chip">Chip 2</button>
          <button class="chip">Chip 3</button>
        </div>
        <div class="pills" style="margin-top: 1rem;">
          <button class="pill">Pill A</button>
          <button class="pill">Pill B</button>
          <button class="pill">Pill C</button>
        </div>
      </div>

      <div class="card" data-halo="2">
        <h3>Color Contrast</h3>
        <p style="color: var(--muted);">WCAG AA compliant text contrast ratios</p>
        <div style="margin-top: 1rem; display: grid; gap: 0.75rem;">
          <div style="padding: 1rem; background: var(--bg-0); border-radius: 0.5rem;">
            <span style="color: var(--ink);">Primary text on dark: 14.6:1 ✓</span>
          </div>
          <div style="padding: 1rem; background: var(--bg-0); border-radius: 0.5rem;">
            <span style="color: var(--muted);">Muted text on dark: 7.2:1 ✓</span>
          </div>
          <div style="padding: 1rem; background: var(--accent-primary); border-radius: 0.5rem;">
            <span style="color: #ffffff;">White on primary: 4.8:1 ✓</span>
          </div>
        </div>
      </div>

      <div class="card" data-halo="2">
        <h3>Reduced Motion</h3>
        <p style="color: var(--muted);">Animations disable when prefers-reduced-motion is set</p>
        <div class="strip" style="--speed: 4s; margin-top: 1rem;"></div>
        <hr class="rule" style="margin: 1.5rem 0;" />
        <p style="font-size: 0.875rem; color: var(--muted);">
          These animations will stop if you enable "Reduce motion" in your OS settings
        </p>
      </div>
    </div>
  </section>

  <!-- Code Examples Section -->
  <section class="section">
    <h2 class="under">Quick Code Examples</h2>
    <div class="card" data-halo="1">
      <h3>Using Tokens in Components</h3>
      
      <details style="margin-top: 1rem;">
        <summary style="cursor: pointer; font-weight: 600; padding: 0.5rem; background: rgba(255,255,255,0.04); border-radius: 0.5rem;">
          Svelte Component
        </summary>
        <pre style="background: #0a0a0a; padding: 1rem; border-radius: 0.5rem; overflow-x: auto; margin-top: 0.5rem;"><code>&lt;script&gt;
  import &#123; tokens &#125; from '$lib/tokens';
&lt;/script&gt;

&lt;div class="card" data-halo="2"&gt;
  &lt;h3 style="color: &#123;tokens.colors.neutral.text.primary&#125;"&gt;
    Card Title
  &lt;/h3&gt;
&lt;/div&gt;</code></pre>
      </details>

      <details style="margin-top: 1rem;">
        <summary style="cursor: pointer; font-weight: 600; padding: 0.5rem; background: rgba(255,255,255,0.04); border-radius: 0.5rem;">
          Plain HTML/CSS
        </summary>
        <pre style="background: #0a0a0a; padding: 1rem; border-radius: 0.5rem; overflow-x: auto; margin-top: 0.5rem;"><code>&lt;div class="card" data-halo="2"&gt;
  &lt;h3&gt;Card Title&lt;/h3&gt;
  &lt;p style="color: var(--muted);"&gt;Description&lt;/p&gt;
  &lt;button class="btn primary"&gt;Action&lt;/button&gt;
&lt;/div&gt;</code></pre>
      </details>

      <details style="margin-top: 1rem;">
        <summary style="cursor: pointer; font-weight: 600; padding: 0.5rem; background: rgba(255,255,255,0.04); border-radius: 0.5rem;">
          ESP-IDF C (Embedded)
        </summary>
        <pre style="background: #0a0a0a; padding: 1rem; border-radius: 0.5rem; overflow-x: auto; margin-top: 0.5rem;"><code>const char* css = 
":root&#123;--accent-primary:#ff2d55;&#125;"
".btn&#123;background:var(--accent-primary);&#125;";</code></pre>
      </details>
    </div>
  </section>
</div>

<style>
  .color-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
    gap: 1rem;
    margin-top: 1.5rem;
  }

  .color-swatch-card {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
  }

  .color-swatch {
    width: 100%;
    height: 80px;
    border-radius: 0.5rem;
    border: 1px solid rgba(255, 255, 255, 0.1);
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
  }

  .color-info {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .color-code, .css-var {
    font-family: ui-monospace, monospace;
    font-size: 0.875rem;
    color: var(--muted);
    background: rgba(255, 255, 255, 0.04);
    padding: 0.2rem 0.4rem;
    border-radius: 0.25rem;
  }

  .color-actions {
    display: flex;
    gap: 0.5rem;
    margin-top: 0.5rem;
  }

  .spacing-demo {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    margin-top: 1.5rem;
  }

  .spacing-item {
    display: flex;
    align-items: center;
    gap: 1rem;
  }

  .spacing-visual {
    height: 32px;
    background: linear-gradient(135deg, var(--cya), var(--mag));
    border-radius: 0.25rem;
  }

  .spacing-label {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .spacing-label code {
    font-size: 0.875rem;
    color: var(--muted);
  }

  .radius-demo {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
    gap: 1.5rem;
    margin-top: 1.5rem;
  }

  .radius-item {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 0.75rem;
  }

  .radius-box {
    width: 80px;
    height: 80px;
    background: linear-gradient(135deg, var(--mag), var(--cya));
    border: 1px solid rgba(255, 255, 255, 0.1);
  }

  .radius-label {
    text-align: center;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .radius-label code {
    font-size: 0.875rem;
    color: var(--muted);
  }

  .halo-demo-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
    gap: 1rem;
    margin-top: 1.5rem;
  }

  .motion-demo {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
    gap: 1.5rem;
    margin-top: 1.5rem;
  }

  .motion-item {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    align-items: center;
  }

  .motion-box {
    width: 120px;
    height: 120px;
    background: linear-gradient(135deg, var(--mag), var(--cya));
    border-radius: 1rem;
    display: flex;
    align-items: center;
    justify-content: center;
    text-align: center;
    font-weight: 600;
    cursor: pointer;
    transition: transform ease-out;
  }

  .motion-box.fast {
    transition-duration: var(--dur-fast);
  }

  .motion-box.normal {
    transition-duration: var(--dur-med);
  }

  .motion-box.slow {
    transition-duration: var(--dur-slow);
  }

  .motion-box:hover {
    transform: scale(1.1);
  }

  .motion-item code {
    font-size: 0.875rem;
    color: var(--muted);
  }

  details {
    background: rgba(255, 255, 255, 0.02);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 0.5rem;
    padding: 0.5rem;
  }

  details[open] summary {
    margin-bottom: 0.5rem;
  }

  pre {
    margin: 0;
  }

  code {
    font-family: ui-monospace, 'Courier New', monospace;
    font-size: 0.9rem;
    line-height: 1.5;
    color: var(--ink);
  }

  @media (max-width: 768px) {
    .color-grid {
      grid-template-columns: 1fr;
    }

    .radius-demo {
      grid-template-columns: repeat(2, 1fr);
    }

    .motion-demo {
      grid-template-columns: 1fr;
    }

    .halo-demo-grid {
      grid-template-columns: repeat(2, 1fr);
    }
  }
</style>
