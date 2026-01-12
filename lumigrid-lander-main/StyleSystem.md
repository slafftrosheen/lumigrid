# LumiGrid Style System

A comprehensive documentation of the styling system for the LumiGrid LED Node landing page. This system is inspired by Apple's design language with vibrant LED brand colors.

**🎨 [View Interactive Demo →](/style-demo)**

---

## Quick Start

### For New Developers

Get started with the LumiGrid design system in 3 steps:

1. **Import Global Styles** — Already done in `src/routes/+layout.svelte`:
   ```svelte
   <script>
     import '$lib/styles.css';
   </script>
   ```

2. **Use CSS Custom Properties** — Reference tokens in your component styles:
   ```css
   .my-component {
     background: var(--glass-bg);
     color: var(--ink);
     padding: var(--spacing-mobile-lg);
     border-radius: 1rem;
   }
   ```

3. **Apply Standard Classes** — Use pre-built component classes:
   ```svelte
   <div class="card" data-halo="2">
     <h3>My Feature</h3>
     <p>Card with soft halo effect</p>
   </div>
   <button class="btn primary">Click Me</button>
   ```

### Common Patterns

**Creating a Card:**
```svelte
<div class="card" data-halo="2">
  <h3>Title</h3>
  <p>Description</p>
</div>
```

**Using Buttons:**
```svelte
<button class="btn primary">Primary Action</button>
<button class="btn outline">Secondary Action</button>
<button class="btn ghost">Tertiary Action</button>
```

**Grid Layouts:**
```svelte
<div class="grid two">
  <div class="card">Card 1</div>
  <div class="card">Card 2</div>
</div>
```

**Programmatic Token Access:**
```svelte
<script>
  import { tokens } from '$lib/tokens';
  const primaryColor = tokens.colors.brand.primary;
</script>
```

### Troubleshooting

**Q: My custom colors aren't working**  
A: Use CSS custom properties: `var(--ink)` instead of hard-coded hex values.

**Q: Animations not respecting reduced-motion**  
A: Motion tokens automatically disable animations when `prefers-reduced-motion: reduce` is active.

**Q: Touch targets too small on mobile**  
A: Use `min-height: var(--touch-target-min, 44px)` for all interactive elements.

**Q: How do I add a new color?**  
A: Add it to both `src/lib/tokens.ts` and `src/lib/styles.css` :root, then document in this file.

---

## Table of Contents

1. [Quick Start](#quick-start) ⬆️
2. [Design Tokens](#design-tokens)
3. [CSS Custom Properties](#css-custom-properties)
4. [Color System](#color-system)
5. [Typography](#typography)
6. [Spacing](#spacing)
7. [Effects & Animations](#effects--animations)
8. [Components](#components)
9. [Responsive Design](#responsive-design)
10. [Accessibility](#accessibility)
11. [Print Styles](#print-styles)
12. [ESP/Pi Adoption Status](#esppi-adoption-status)
13. [Best Practices](#best-practices)

---

## Design Tokens

The design token system is defined in `src/lib/tokens.ts` and provides a JavaScript/TypeScript interface for the design system. These tokens serve as a reference and can be used in JavaScript/TypeScript code for dynamic styling. The actual styling implementation uses CSS custom properties defined in `src/lib/styles.css`.

### Token Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Design Token System                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  src/lib/tokens.ts          src/lib/styles.css             │
│  ├─ colors                  ├─ :root CSS variables         │
│  │  ├─ brand                │  ├─ Background colors        │
│  │  ├─ neutral              │  ├─ LED brand colors         │
│  │  └─ glass                │  ├─ Gradients                │
│  ├─ spacing                 │  ├─ Glassmorphism            │
│  ├─ radius                  │  ├─ Halo effects             │
│  ├─ transitions             │  └─ Motion tokens            │
│  ├─ typography              │                               │
│  └─ effects                 └─ Component styles            │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Complete Token Reference Map

| Token Category | tokens.ts Export | CSS Variable(s) | Usage Context |
|----------------|------------------|-----------------|---------------|
| **Brand Primary** | `colors.brand.primary` | `--accent-primary` | CTA buttons, links, focus states |
| **LED Cyan** | `colors.brand.ledCyan` | `--cya`, `--c`, `--rf-cyan`, `--halo-primary` | LED effects, halos, gradients |
| **LED Amber** | `colors.brand.ledAmber` | `--amb`, `--warm`, `--rf-amber`, `--halo-glow` | Warm accents, glow effects |
| **LED Magenta** | `colors.brand.ledMagenta` | `--mag`, `--a`, `--rf-magenta`, `--halo-secondary` | Vibrant accents, halos |
| **LED Cherry** | `colors.brand.ledCherry` | `--cherry`, `--b`, `--rf-cherry`, `--accent-cherry` | Alert states, emphasis |
| **Background Base** | `colors.neutral.bg.base` | `--bg` | Body background |
| **Background Primary** | `colors.neutral.bg.primary` | `--bg-0` | Deepest background layer |
| **Background Secondary** | `colors.neutral.bg.secondary` | `--bg-1` | Card backgrounds, glass |
| **Text Primary** | `colors.neutral.text.primary` | `--ink` | Main body text, headings |
| **Text Secondary** | `colors.neutral.text.secondary` | `--muted` | Captions, labels |
| **Glass Background** | `colors.glass.bg` | `--glass-bg`, `--glass-1` | Card surfaces |
| **Glass Border** | `colors.glass.border` | `--glass-border` | Card borders |
| **Spacing XS-XXL** | `spacing.*` | `--spacing-mobile-*` | Layouts, margins, padding |
| **Border Radius** | `radius.*` | Applied inline | Cards, buttons, inputs |
| **Transitions** | `transitions.*` | `--dur-fast/med/slow`, `--ease-*` | Animations, state changes |
| **Typography** | `typography.*` | `--font-display/body`, `--fs-*`, `--fw-*` | Text rendering |
| **Shadows** | `effects.shadow.*` | `--shadow` | Depth, elevation |

### Brand Colors

```typescript
colors: {
  brand: {
    primary: '#0071e3',      // Apple blue accent
    ledCyan: '#00d4ff',      // LED cyan
    ledAmber: '#ff9500',     // LED amber
    ledMagenta: '#ff2d55',   // LED magenta
    ledCherry: '#ff3b30',    // LED cherry/red
  }
}
```

### Neutral Colors

```typescript
neutral: {
  bg: {
    primary: '#000000',      // --bg-0
    secondary: '#1d1d1f',    // --bg-1
    tertiary: '#2d2d2f',
    base: '#050505',         // --bg
  },
  text: {
    primary: '#f5f5f7',      // --ink
    secondary: '#a1a1a6',    // --muted
    tertiary: '#6e6e73',
  }
}
```

### Glass Effect Colors

```typescript
glass: {
  bg: 'rgba(29, 29, 31, 0.72)',
  border: 'rgba(255, 255, 255, 0.1)',
}
```

### Spacing Scale

```typescript
spacing: {
  xs: '0.5rem',    // 8px
  sm: '1rem',      // 16px
  md: '1.5rem',    // 24px
  lg: '2rem',      // 32px
  xl: '3rem',      // 48px
  xxl: '4rem',     // 64px
}
```

### Border Radius

```typescript
radius: {
  sm: '8px',
  md: '12px',
  lg: '18px',
  xl: '24px',
  full: '9999px',
}
```

### Transitions

```typescript
transitions: {
  fast: '150ms',
  normal: '300ms',
  slow: '500ms',
  easing: {
    default: 'cubic-bezier(0.4, 0, 0.2, 1)',
    in: 'cubic-bezier(0.4, 0, 1, 1)',
    out: 'cubic-bezier(0, 0, 0.2, 1)',
  }
}
```

---

## CSS Custom Properties

The CSS custom properties are defined in `src/lib/styles.css` and provide the foundational styling variables.

### Background Colors

```css
:root {
  --bg-0: #000000;           /* Deepest background */
  --bg-1: #1d1d1f;           /* Secondary background */
  --bg: #050505;             /* Base background */
}
```

### Text Colors

```css
:root {
  --ink: #f5f5f7;            /* Primary text color */
  --muted: #a1a1a6;          /* Secondary/muted text */
}
```

### LED Brand Colors

```css
:root {
  /* Primary LED colors */
  --mag: #ff2d55;            /* Magenta */
  --cherry: #ff3b30;         /* Cherry red */
  --cya: #00d4ff;            /* Cyan */
  --amb: #ff9500;            /* Amber */
  
  /* Shorthand aliases */
  --a: #ff2d55;
  --b: #ff3b30;
  --c: #00d4ff;
  --warm: #ff9500;
  
  /* Extended brand hues (rf- prefix) */
  --rf-magenta: #ff2d55;
  --rf-cherry: #ff3b30;
  --rf-cyan: #00d4ff;
  --rf-amber: #ff9500;
}
```

### Accent Colors

```css
:root {
  --accent-primary: #0071e3;    /* Apple blue */
  --accent-cherry: #ff3b30;     /* Cherry for accents */
}
```

### Gradients

```css
:root {
  /* Primary gradient (vibrant LED colors) */
  --primary-grad: linear-gradient(135deg, #ff2d55, #ff3b30, #00d4ff);
  --grad-rf: linear-gradient(135deg, #ff2d55, #ff3b30, #00d4ff);
  --grad-rf-alt: linear-gradient(135deg, var(--rf-cherry), var(--rf-cyan), var(--rf-magenta));
  
  /* Warm gradient */
  --grad-warm: linear-gradient(135deg, #ff3b30, #ff9500);
}
```

### Glassmorphism

```css
:root {
  /* Glass tiers */
  --glass-0: rgba(29, 29, 31, 0.65);
  --glass-1: rgba(29, 29, 31, 0.72);
  --glass-2: rgba(29, 29, 31, 0.80);
  --glass-bg: rgba(29, 29, 31, 0.72);
  --glass-border: rgba(255, 255, 255, 0.1);
  --glass-blur: blur(20px);
}
```

### Halo Effects

```css
:root {
  /* Halo intensity grades (for data-halo attribute) */
  --halo-0: 0;         /* None */
  --halo-1: 0.06;      /* Subtle */
  --halo-2: 0.10;      /* Soft */
  --halo-3: 0.16;      /* Strong */
  --halo-4: 0.22;      /* Vivid */
  
  /* Halo gradient colors */
  --halo-primary: #00d4ff;     /* LED cyan */
  --halo-secondary: #ff2d55;   /* LED magenta */
  --halo-glow: #ff9500;        /* LED amber */
  
  /* Halo animation speeds */
  --halo-speed: 18s;
  --halo-speed-alt: 22s;
  --halo-sheen-speed: 14s;
  --halo-sheen-blur: 80px;
}
```

### Glow & Shadow Effects

```css
:root {
  --glow-soft: 0.16;
  --glow-strong: 0.24;
  --halo: 96px;
  --ring: var(--accent-primary);
  --shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
}
```

### Motion Tokens

```css
:root {
  --ease-out: cubic-bezier(0.2, 0.8, 0.2, 1);
  --ease-default: cubic-bezier(0.4, 0, 0.2, 1);
  --ease-in: cubic-bezier(0.4, 0, 1, 1);
  --dur-fast: 150ms;
  --dur-med: 300ms;
  --dur-slow: 500ms;
}

/* Reduced motion support */
@media (prefers-reduced-motion: reduce) {
  :root {
    --dur-fast: 0ms;
    --dur-med: 0ms;
    --dur-slow: 0ms;
  }
}
```

---

## Typography

### Font Families

```css
:root {
  /* Display font (headings) */
  --font-display: -apple-system, BlinkMacSystemFont, "SF Pro Display", system-ui, sans-serif;
  
  /* Body font (text content) */
  --font-body: -apple-system, BlinkMacSystemFont, "SF Pro Text", Inter, system-ui, sans-serif;
}
```

### Font Size Scale

```css
:root {
  /* Modern fluid typography scale */
  --fs-display-xl: clamp(3rem, 8vw, 6rem);        /* Hero headlines */
  --fs-display-lg: clamp(2rem, 5vw, 3.5rem);      /* Section titles */
  --fs-title-1: clamp(1.75rem, 4vw, 2.5rem);      /* Card titles */
  --fs-title-2: clamp(1.5rem, 3vw, 2rem);         /* Subsection titles */
  --fs-body-lg: clamp(1.0625rem, 2vw, 1.25rem);   /* Lead paragraphs */
  --fs-body: 1rem;                                 /* Body text (16px) */
  --fs-caption: 0.875rem;                          /* Small text (14px) */
  
  /* Legacy aliases */
  --fs-h1: var(--fs-display-xl);
  --fs-h2: var(--fs-display-lg);
  --fs-h3: var(--fs-title-1);
  --fs: var(--fs-body);
  --fs-lead: var(--fs-body-lg);
  --fs-big: var(--fs-body-lg);
}
```

### Font Weights

```css
:root {
  --fw-regular: 400;
  --fw-medium: 500;
  --fw-semibold: 600;
  --fw-bold: 700;
}
```

### Heading Styles

All headings use a gradient with animation:

```css
h1, h2, h3 {
  background: linear-gradient(135deg, #ffffff, #e0e0e0, #d0d0d0);
  background-size: 200% 200%;
  -webkit-background-clip: text;
  background-clip: text;
  -webkit-text-fill-color: transparent;
  animation: gradient-shift 8s ease infinite;
  text-wrap: balance;
}

@keyframes gradient-shift {
  0%, 100% { background-position: 0% 50%; }
  50% { background-position: 100% 50%; }
}
```

---

## Spacing

### Mobile Spacing Tokens

```css
:root {
  --spacing-mobile-xs: 0.5rem;    /* 8px */
  --spacing-mobile-sm: 1rem;      /* 16px */
  --spacing-mobile-md: 1.5rem;    /* 24px */
  --spacing-mobile-lg: 2rem;      /* 32px */
  --spacing-mobile-xl: 3rem;      /* 48px */
}
```

### Layout Containers

```css
.container {
  width: min(1200px, 92vw);
  margin: 0 auto;
}

.section {
  padding: clamp(2rem, 5vw, 4rem) 0;
}
```

---

## Effects & Animations

### Keyframe Animations

```css
/* Gradient sweep animation */
@keyframes sweep {
  to { translate: 50% 0; rotate: 1turn; }
}

/* Flow animation for LED strips */
@keyframes flow {
  to { transform: translateX(30%); }
}

/* Move animation */
@keyframes move {
  to { transform: translateX(30%); }
}

/* Spin animation for gradient frames */
@keyframes spin {
  to { transform: rotate(1turn); }
}

/* Underline animation */
@keyframes underline {
  50% { left: 60%; right: 0; }
}

/* Pulse glow for accent text */
@keyframes pulse-glow {
  0%, 100% {
    text-shadow: 0 0 4px rgba(220, 38, 38, 0.3),
                 0 0 8px rgba(220, 38, 38, 0.2);
  }
  50% {
    text-shadow: 0 0 8px rgba(220, 38, 38, 0.5),
                 0 0 16px rgba(220, 38, 38, 0.3),
                 0 0 24px rgba(220, 38, 38, 0.1);
  }
}
```

### Reveal Animation

```css
.reveal {
  opacity: 0;
  transform: translateY(16px) scale(0.98);
}

.reveal.in {
  opacity: 1;
  transform: none;
  transition: transform var(--dur-med) var(--ease-out),
              opacity var(--dur-med) var(--ease-out);
}
```

### Halo Effect System

Use the `data-halo` attribute to apply tokenized halo effects:

```html
<div data-halo="2">Content with soft halo</div>
```

Available levels:
- `data-halo="0"` - No halo
- `data-halo="1"` - Subtle (6% opacity)
- `data-halo="2"` - Soft (10% opacity)
- `data-halo="3"` - Strong (16% opacity)
- `data-halo="4"` - Vivid (22% opacity)

```css
[data-halo] {
  position: relative;
  isolation: isolate;
  background: var(--glass-1);
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 1rem;
}

[data-halo]::before {
  content: "";
  position: absolute;
  inset: -2px;
  border-radius: inherit;
  z-index: -1;
  background:
    radial-gradient(120% 160% at 20% 20%, 
      color-mix(in oklab, var(--halo-primary) 35%, rgba(255,255,255,.15)) 0 30%, 
      transparent 60%),
    radial-gradient(120% 160% at 80% 60%, 
      color-mix(in oklab, var(--halo-secondary) 30%, rgba(200,200,200,.15)) 0 30%, 
      transparent 60%),
    radial-gradient(140% 180% at 50% 50%, 
      color-mix(in oklab, var(--halo-glow) 20%, transparent) 0 40%, 
      transparent 70%);
  opacity: var(--halo-amount, 0.10);
  filter: blur(24px);
}
```

---

## Components

### Cards

```css
.card {
  position: relative;
  background: var(--glass-bg);
  backdrop-filter: var(--glass-blur);
  -webkit-backdrop-filter: var(--glass-blur);
  border: 1px solid var(--glass-border);
  border-radius: 18px;
  padding: clamp(1.5rem, 3vw, 2.5rem);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1), 
              0 8px 24px rgba(0, 0, 0, 0.15);
  transition: transform 0.3s var(--ease-default), 
              box-shadow 0.3s var(--ease-default);
  contain: content;
  content-visibility: auto;
  contain-intrinsic-size: 400px 300px;
}

/* Card inner glow */
.card::before {
  content: "";
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 1px;
  background: linear-gradient(90deg,
    rgba(255,255,255,.3),
    rgba(255,255,255,.1));
  pointer-events: none;
  opacity: .7;
}

/* Card ambient glow */
.card::after {
  content: "";
  position: absolute;
  inset: 0;
  border-radius: inherit;
  background: radial-gradient(120% 120% at 50% -20%, 
    rgba(255,255,255,.06), transparent 55%);
  mix-blend-mode: screen;
  pointer-events: none;
  opacity: .75;
}
```

### Buttons

```css
.btn {
  display: inline-flex;
  align-items: center;
  gap: 0.5rem;
  font-weight: 600;
  padding: 0.75rem 1rem;
  border-radius: 1rem;
  border: 2px solid transparent;
  background: rgba(255, 255, 255, 0.08);
  color: var(--ink);
  text-decoration: none;
  cursor: pointer;
  position: relative;
  overflow: hidden;
  transition: transform var(--dur-med) var(--ease-out),
              box-shadow var(--dur-med) var(--ease-out),
              background var(--dur-med) var(--ease-out);
}

.btn:hover {
  transform: translateY(-2px);
  box-shadow: 0 16px 32px rgba(8, 17, 35, 0.4),
              0 4px 12px rgba(8, 17, 35, 0.2);
  background: rgba(255, 255, 255, 0.12);
}

/* Primary button */
.btn.primary {
  background: var(--accent-primary);
  color: #ffffff;
  border-radius: 0.85rem;
  padding: 0.65rem 1rem;
  font-weight: 600;
  border: 0;
  box-shadow: 0 4px 12px rgba(0, 113, 227, 0.3);
}

.btn.primary:hover {
  background: #0077ed;
  filter: saturate(1.15) brightness(1.08);
  box-shadow: 0 8px 24px rgba(0, 113, 227, 0.4),
              0 2px 8px rgba(0, 113, 227, 0.2);
  transform: translateY(-3px);
}

/* Outline button */
.btn.outline {
  border-color: var(--accent-primary);
  color: var(--accent-primary);
  background: transparent;
}

/* Ghost button */
.btn.ghost {
  background: rgba(255, 255, 255, 0.06);
  border-color: rgba(255, 255, 255, 0.15);
  color: var(--ink);
}

/* Big button */
.btn.big {
  padding: 1rem 1.25rem;
  font-size: 1.05rem;
}

/* Brand button */
.btn.brand {
  font-weight: 600;
  letter-spacing: 0.02em;
  padding-inline: 0.75rem 0.95rem;
  background: rgba(255, 255, 255, 0.08);
  box-shadow: 0 8px 32px rgba(9, 14, 30, 0.45);
}
```

### Pills & Chips

```css
/* Pills (info toggles) */
.pills, .pill-row {
  display: flex;
  gap: 0.7rem;
  flex-wrap: wrap;
  margin: 1rem 0 1.25rem;
}

.pill {
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(255, 255, 255, 0.15);
  color: var(--ink);
  border-radius: 999px;
  padding: 0.5rem 0.9rem;
  cursor: pointer;
  transition: background 0.3s ease, border-color 0.3s ease;
  font-size: inherit;
  font-family: inherit;
}

.pill[aria-selected="true"],
.pill[aria-pressed="true"] {
  background: #1a1a1a;
  border-color: rgba(255, 255, 255, 0.3);
}

/* Chips */
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
}

.chip {
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 999px;
  padding: 0.35rem 0.7rem;
  color: var(--ink);
  cursor: pointer;
  transition: background 0.3s ease, border-color 0.3s ease, box-shadow 0.3s ease;
  font-family: inherit;
  font-size: inherit;
}

.chip[aria-selected="true"] {
  background: rgba(255, 255, 255, 0.12);
}

.chip[aria-checked="true"],
.chips .chip:focus-visible {
  background: rgba(255, 255, 255, 0.18);
  border-color: rgba(255, 255, 255, 0.25);
  box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.15);
}
```

### LED Strips

```css
.strip {
  height: 16px;
  border-radius: 999px;
  overflow: hidden;
  background: #0a0a0a;
  box-shadow: 0 0 18px rgba(255, 255, 255, 0.15);
  position: relative;
  margin: 12px 0;
}

.strip::before {
  content: "";
  position: absolute;
  inset: 0;
  background: repeating-linear-gradient(90deg,
    rgba(255, 255, 255, 0.95) 0 6px,
    rgba(220, 220, 220, 0.95) 6px 12px,
    rgba(200, 200, 200, 0.95) 12px 18px,
    rgba(240, 240, 240, 0.95) 18px 24px);
  filter: blur(0.6px);
  transform: translateX(-30%);
  animation: move var(--speed, 6s) linear infinite;
}
```

### Gradient Frame

```css
.grad-frame {
  position: relative;
  border-radius: 1rem;
  background: var(--glass-1);
  border: 1px solid rgba(255, 255, 255, 0.14);
  overflow: hidden;
  isolation: isolate;
}

.grad-frame::before {
  content: "";
  position: absolute;
  inset: 0;
  z-index: -2;
  pointer-events: none;
  background: conic-gradient(from 0deg, #ffffff, #e0e0e0, #d0d0d0, #ffffff);
  filter: blur(10px);
  opacity: 0.25;
  animation: spin 16s linear infinite;
}

.grad-frame::after {
  content: "";
  position: absolute;
  inset: 1px;
  border-radius: calc(1rem - 1px);
  background: var(--glass-1);
  pointer-events: none;
  z-index: -1;
}
```

### Animated Rule (HR)

```css
hr.rule {
  height: 2px;
  border: 0;
  border-radius: 999px;
  overflow: hidden;
  background: #0a0a0a;
  position: relative;
  isolation: isolate;
  margin: clamp(12px, 2vh, 24px) auto;
  max-width: min(1080px, 92vw);
  opacity: 0.85;
}

hr.rule::after {
  content: "";
  position: absolute;
  inset: 0;
  translate: -50% 0;
  width: 120%;
  background: conic-gradient(from 0deg, #ffffff, #e0e0e0, #d0d0d0, #ffffff);
  filter: blur(0.6px);
  animation: sweep 8s linear infinite;
}
```

### Form Controls

```css
label {
  display: grid;
  gap: 0.35rem;
  font-weight: 600;
}

input, textarea {
  font: inherit;
  padding: 0.65rem 0.75rem;
  border-radius: 0.6rem;
  border: 1px solid rgba(255, 255, 255, 0.2);
  background: rgba(10, 10, 10, 0.65);
  color: var(--ink);
}

textarea {
  min-height: 120px;
  resize: vertical;
}

input[type="range"] {
  width: 170px;
  cursor: pointer;
}
```

### Grid Layouts

```css
.grid {
  display: grid;
  gap: clamp(1rem, 2.5vw, 2rem);
}

.grid.two, .grid-2 {
  grid-template-columns: repeat(2, minmax(0, 1fr));
}

.grid.three {
  grid-template-columns: repeat(3, minmax(0, 1fr));
}

@media (max-width: 1024px) {
  .grid.two, .grid-2 {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 900px) {
  .grid.three {
    grid-template-columns: 1fr;
  }
}
```

---

## Responsive Design

### Breakpoints

```css
:root {
  --mobile-sm: 480px;
  --mobile-md: 768px;
  --mobile-lg: 1024px;
  
  /* Touch target minimum */
  --touch-target-min: 44px;
  
  /* Mobile nav height */
  --nav-height-mobile: 64px;
}
```

### Tablet and Below (max-width: 1024px)

```css
@media (max-width: 1024px) {
  .container {
    width: min(1200px, 94vw);
  }

  .section {
    padding: clamp(1.5rem, 4vw, 3rem) 0;
  }

  .card {
    padding: clamp(1rem, 3vw, 1.5rem);
  }

  .grid.two, .grid-2 {
    grid-template-columns: 1fr;
    gap: clamp(1rem, 2.5vw, 1.5rem);
  }

  .grid.three {
    grid-template-columns: repeat(2, 1fr);
    gap: clamp(0.875rem, 2vw, 1.25rem);
  }
}
```

### Mobile Landscape and Below (max-width: 768px)

```css
@media (max-width: 768px) {
  body {
    font-size: clamp(0.9375rem, 4vw, 1rem);
  }

  .container {
    width: min(1200px, 96vw);
  }

  .section {
    padding: clamp(1.25rem, 3.5vw, 2.5rem) 0;
  }

  .hero {
    padding-top: clamp(1.5rem, 6vh, 4rem);
    gap: 0.875rem;
  }

  .card {
    border-radius: 0.875rem;
    padding: clamp(0.875rem, 2.5vw, 1.25rem);
  }

  .btn {
    padding: 0.625rem 0.875rem;
    font-size: 0.9375rem;
    min-height: var(--touch-target-min);
  }

  /* Optimize halo effects for mobile performance */
  [data-halo]::before {
    filter: blur(16px);
  }

  /* Reduce animations on touch devices */
  @media (hover: none) {
    * {
      animation-duration: 0.3s !important;
    }
    
    [data-halo].card:hover,
    [data-halo].card:focus-within {
      transform: none;
    }

    .btn:hover {
      transform: none;
    }
  }
}
```

### Small Mobile Portrait (max-width: 480px)

```css
@media (max-width: 480px) {
  .container {
    width: 98vw;
  }

  .section {
    padding: clamp(1rem, 3vw, 2rem) 0;
  }

  .hero {
    padding-top: clamp(1rem, 4vh, 2.5rem);
  }

  .card {
    border-radius: 0.75rem;
    padding: 0.875rem;
  }

  .btn {
    padding: 0.5rem 0.75rem;
    font-size: 0.875rem;
  }

  /* Stack buttons vertically on very small screens */
  .hero .btn-group {
    flex-direction: column;
    width: 100%;
  }

  .hero .btn-group .btn {
    width: 100%;
    justify-content: center;
  }

  /* Better text wrapping */
  h1, h2, h3 {
    overflow-wrap: break-word;
    word-wrap: break-word;
    hyphens: auto;
  }
}
```

### Touch Device Optimizations

```css
@media (hover: none) and (pointer: coarse) {
  /* Larger tap targets for touch devices */
  button, a, input, select, textarea, .chip, .pill {
    min-height: var(--touch-target-min);
  }

  /* Remove hover effects on touch devices */
  .btn:hover::after {
    opacity: 0;
  }

  /* Simplify animations for better performance */
  * {
    animation-duration: 0.5s !important;
  }

  /* Reduce backdrop blur for better performance */
  [style*="backdrop-filter"] {
    backdrop-filter: blur(8px) !important;
  }
}
```

---

## Accessibility

### Focus States

```css
/* Enhanced focus states for accessibility */
:where(button, a, [role="button"], input, select, textarea):focus-visible {
  outline: 2px solid var(--accent-primary);
  outline-offset: 4px;
  border-radius: 4px;
}

/* Remove default outline for non-keyboard focus */
:where(button, a, [role="button"], input, select, textarea):focus:not(:focus-visible) {
  outline: none;
}
```

### Skip Link

```css
.skip-link {
  position: absolute;
  left: 0;
  top: -40px;
  background: #111;
  color: #fff;
  padding: 0.5rem 0.75rem;
  z-index: 1000;
  text-decoration: none;
}

.skip-link:focus {
  top: 0.5rem;
}
```

### Reduced Motion Support

```css
@media (prefers-reduced-motion: reduce) {
  :root {
    --dur-fast: 0ms;
    --dur-med: 0ms;
    --dur-slow: 0ms;
  }
  
  * {
    animation: none !important;
    transition: none !important;
  }
}
```

### Touch Targets

All interactive elements have a minimum touch target of 44px:

```css
button, .chip, .btn, a.card {
  min-height: 44px;
}
```

### Color Contrast

The system uses high contrast colors:
- Primary text (`--ink`): #f5f5f7 on dark backgrounds
- Muted text (`--muted`): #a1a1a6 on dark backgrounds
- Accent color (`--accent-primary`): #0071e3

---

## Light Mode Support

```css
@media (prefers-color-scheme: light) {
  :root {
    --bg-0: #ffffff;
    --bg-1: #f8f8f8;
    --bg: #fafafa;
    --ink: #1d1d1f;
    --muted: #6e6e73;
    
    /* Vibrant gradients in light mode */
    --primary-grad: linear-gradient(135deg, #ff2d55, #ff3b30, #00d4ff);
    --grad-rf: linear-gradient(135deg, #ff2d55, #ff3b30, #00d4ff);
    
    /* Halo colors (same in light mode) */
    --halo-primary: #00d4ff;
    --halo-secondary: #ff2d55;
    --halo-glow: #ff9500;
    
    /* Glass adjustments for light mode */
    --glass-0: rgba(248, 248, 248, 0.65);
    --glass-1: rgba(248, 248, 248, 0.72);
    --glass-2: rgba(248, 248, 248, 0.80);
    --glass-bg: rgba(248, 248, 248, 0.72);
  }
}
```

---

## Print Styles

Print styles are defined in `static/print.css`. The following is a formatted version for readability (the actual file uses a more condensed format):

```css
@media print {
  @page {
    size: A4;
    margin: 14mm;
  }
  
  body {
    background: #fff !important;
  }
  
  /* Hide ambient effects and navigation */
  canvas, .header, .nav-btn, hr.rule, .LightSettings {
    display: none !important;
  }
  
  /* Cards become white with hairline border */
  .card, .grad-frame {
    background: #fff !important;
    border: 1px solid #e5e7eb !important;
    box-shadow: none !important;
  }
  
  .grad-frame::before,
  .grad-frame::after {
    display: none !important;
  }
  
  /* Show link URLs */
  a::after {
    content: " (" attr(href) ")";
    font-size: 0.85em;
    color: #6b7280;
  }
}
```

---

## Z-Index Layering System

The following z-index layers are used to manage stacking context across the site:

```
-5: Brand gradient (body::before pseudo-element, the deepest background layer)
-4 to -1: LumenField canvas layers (animated background components):
    - Dot mesh: particle grid effect
    - Halos: ambient glow effects
    - Filaments: connecting line effects
    - Cursor flare: mouse-following light effect
0+: Content (header, cards, text, and all UI elements)
25: Language toggle (positioned fixed in top-right corner)
39: Mobile backdrop (overlay behind mobile menu)
40: Header (sticky navigation), Mobile menu (slide-out panel)
50: Hamburger button (when active, above mobile menu)
1000: Skip link (accessibility feature, highest priority when focused)
```

---

## Component-Specific Styles

### Header

The header uses glassmorphism with a sticky position:

```css
.header {
  position: sticky;
  top: 0;
  z-index: 40;
  background: rgba(0, 0, 0, 0.72) !important;
  backdrop-filter: saturate(180%) blur(20px);
  -webkit-backdrop-filter: saturate(180%) blur(20px);
}

.header::after {
  content: "";
  position: absolute;
  left: 0;
  right: 0;
  bottom: 0;
  height: 1px;
  background: linear-gradient(90deg,
    rgba(255, 255, 255, 0.18),
    rgba(255, 255, 255, 0.02),
    rgba(255, 255, 255, 0.18));
  opacity: 0.5;
}
```

### Hero Section

```css
.hero {
  display: grid;
  gap: 1rem;
  padding-top: clamp(2rem, 10vh, 7rem);
}

.hero h1 {
  font-size: var(--fs-h1);
  line-height: 1.12;
  margin: 0;
  background: linear-gradient(135deg, #ffffff, #e0e0e0);
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
}

.hero p.lead, .lead {
  font-size: var(--fs-lead);
  color: var(--muted);
  margin: 0.5rem 0 0;
}
```

### Footer

```css
.site-footer {
  border-top: 1px solid rgba(255, 255, 255, 0.12);
  padding: 2rem 0;
  background: #000000;
}

.footer-grid {
  display: grid;
  grid-template-columns: 1fr auto auto;
  gap: 1rem;
  align-items: start;
}

.site-footer a {
  color: var(--muted);
  text-decoration: none;
}

@media (max-width: 900px) {
  .footer-grid {
    grid-template-columns: 1fr;
    text-align: center;
  }
}
```

---

## Usage Examples

### Creating a Card with Halo Effect

```html
<div class="card" data-halo="2">
  <h3>Card Title</h3>
  <p>Card content goes here.</p>
</div>
```

### Using Gradient Frame

```html
<article class="grad-frame">
  <h3>Feature Title</h3>
  <p>Feature description.</p>
</article>
```

### Button Variants

```html
<!-- Primary button -->
<a href="#" class="btn primary">Get Started</a>

<!-- Outline button -->
<a href="#" class="btn outline">Learn More</a>

<!-- Ghost button -->
<button class="btn ghost">Settings</button>

<!-- Big button -->
<a href="#" class="btn primary big">Start Free Trial</a>
```

### Grid Layout

```html
<div class="grid two">
  <div class="card">Card 1</div>
  <div class="card">Card 2</div>
</div>

<div class="grid three">
  <div class="card">Card 1</div>
  <div class="card">Card 2</div>
  <div class="card">Card 3</div>
</div>
```

### Reveal Animation

```html
<section class="section container reveal">
  <h2>Section Title</h2>
  <p>Content that reveals on scroll.</p>
</section>
```

---

## Cross-Platform Code Examples

### Using Design Tokens in Svelte Components

```svelte
<script>
  import { tokens } from '$lib/tokens';
  
  // Access tokens programmatically
  const primaryColor = tokens.colors.brand.primary;
  const cardPadding = tokens.spacing.lg;
</script>

<div class="custom-card" style="
  background: {tokens.colors.glass.bg};
  border: 1px solid {tokens.colors.glass.border};
  border-radius: {tokens.radius.lg};
  padding: {tokens.spacing.lg};
">
  <h3 style="color: {tokens.colors.neutral.text.primary}">
    Dynamic Card
  </h3>
  <p style="color: {tokens.colors.neutral.text.secondary}">
    This card uses tokens for all styling values.
  </p>
</div>

<style>
  /* Prefer CSS custom properties for static styles */
  .custom-card {
    background: var(--glass-bg);
    backdrop-filter: var(--glass-blur);
    transition: transform var(--dur-med) var(--ease-default);
  }
  
  .custom-card:hover {
    transform: translateY(-2px);
  }
</style>
```

### Plain HTML/CSS Usage

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>LumiGrid Styles</title>
  <link rel="stylesheet" href="styles.css">
</head>
<body>
  <!-- Hero Section -->
  <section class="hero center">
    <h1>Welcome to LumiGrid</h1>
    <p class="lead">Intelligent LED control for creative installations</p>
    <div style="display: flex; gap: 1rem; justify-content: center;">
      <a href="#" class="btn primary">Get Started</a>
      <a href="#" class="btn outline">Learn More</a>
    </div>
  </section>

  <!-- Feature Cards with Halo Effects -->
  <section class="section container">
    <div class="grid two">
      <div class="card" data-halo="2">
        <h3>Sync Protocol</h3>
        <p>Real-time synchronization across all nodes</p>
      </div>
      <div class="card" data-halo="2">
        <h3>Effect Engine</h3>
        <p>Rich library of procedural lighting effects</p>
      </div>
    </div>
  </section>

  <!-- Gradient Frame Example -->
  <div class="grad-frame" style="padding: 2rem; margin: 2rem auto; max-width: 600px;">
    <h3>Animated Border</h3>
    <p>This frame has a rotating gradient border effect</p>
  </div>
</body>
</html>
```

### ESP-IDF C Web UI Examples

For ESP32-based LumiGrid nodes, the web UI is served from flash memory. Here's how to use the design tokens:

```c
// In your ESP-IDF web server handler
static const char* html_header = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
":root{"
"  --bg-0:#000000;"
"  --bg-1:#1d1d1f;"
"  --ink:#f5f5f7;"
"  --muted:#a1a1a6;"
"  --accent-primary:#0071e3;"
"  --glass-bg:rgba(29,29,31,.72);"
"  --glass-border:rgba(255,255,255,.1);"
"  --dur-med:300ms;"
"  --ease-default:cubic-bezier(.4,0,.2,1);"
"}"
"body{"
"  margin:0;"
"  padding:20px;"
"  background:var(--bg-0);"
"  color:var(--ink);"
"  font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
"}"
".card{"
"  background:var(--glass-bg);"
"  border:1px solid var(--glass-border);"
"  border-radius:12px;"
"  padding:1.5rem;"
"  margin-bottom:1rem;"
"}"
".btn{"
"  display:inline-block;"
"  padding:.65rem 1rem;"
"  background:var(--accent-primary);"
"  color:#fff;"
"  border:0;"
"  border-radius:.85rem;"
"  cursor:pointer;"
"  transition:transform var(--dur-med) var(--ease-default);"
"}"
".btn:active{transform:translateY(2px);}"
"</style>"
"</head>"
"<body>";

static const char* control_panel = 
"<div class='card'>"
"  <h2>LED Node Control</h2>"
"  <p style='color:var(--muted)'>Status: <span id='status'>Connected</span></p>"
"  <button class='btn' onclick='toggleLED()'>Toggle LEDs</button>"
"</div>"
"<script>"
"function toggleLED(){"
"  fetch('/api/trigger',{method:'POST',body:JSON.stringify({action:'blackout'})})"
"  .then(r=>r.json())"
"  .then(d=>document.getElementById('status').textContent=d.state);"
"}"
"</script>";

// In your HTTP handler
esp_err_t control_panel_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, html_header);
    httpd_resp_sendstr(req, control_panel);
    httpd_resp_sendstr(req, "</body></html>");
    return ESP_OK;
}
```

### Flask/Jinja2 Template Examples

For Python-based control dashboards or Raspberry Pi gateways:

```python
# Flask app with LumiGrid styles
from flask import Flask, render_template

app = Flask(__name__)

@app.route('/')
def dashboard():
    nodes = [
        {'name': 'LED Node 1', 'status': 'active', 'ip': '192.168.1.101'},
        {'name': 'LED Node 2', 'status': 'active', 'ip': '192.168.1.102'},
    ]
    return render_template('dashboard.html', nodes=nodes)
```

```html
<!-- templates/dashboard.html -->
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>LumiGrid Dashboard</title>
  <link rel="stylesheet" href="{{ url_for('static', filename='lumigrid-styles.css') }}">
</head>
<body>
  <div class="container">
    <section class="hero center">
      <h1>LumiGrid Network</h1>
      <p class="lead">{{ nodes|length }} nodes connected</p>
    </section>

    <section class="section">
      <div class="grid two">
        {% for node in nodes %}
        <div class="card" data-halo="2">
          <h3>{{ node.name }}</h3>
          <p class="muted">IP: {{ node.ip }}</p>
          <div class="pills">
            <span class="pill" aria-selected="true">{{ node.status }}</span>
          </div>
          <a href="http://{{ node.ip }}" class="btn primary">Open Control Panel</a>
        </div>
        {% endfor %}
      </div>
    </section>
  </div>
</body>
</html>
```

### JavaScript Token Access

```javascript
// Import tokens in a vanilla JS project
import { tokens } from './tokens.js';

// Create dynamic UI elements
function createStatusCard(status) {
  const card = document.createElement('div');
  card.className = 'card';
  card.setAttribute('data-halo', '2');
  
  // Use tokens programmatically
  card.style.padding = tokens.spacing.lg;
  card.style.borderRadius = tokens.radius.lg;
  
  const title = document.createElement('h3');
  title.textContent = status.title;
  title.style.color = tokens.colors.neutral.text.primary;
  
  const description = document.createElement('p');
  description.textContent = status.message;
  description.style.color = tokens.colors.neutral.text.secondary;
  
  card.appendChild(title);
  card.appendChild(description);
  
  return card;
}

// Apply theme changes dynamically
function applyLEDColor(color) {
  document.documentElement.style.setProperty(
    '--halo-primary',
    tokens.colors.brand[color]
  );
}

// Animation using motion tokens
function animateCard(element) {
  element.style.transition = `transform ${tokens.transitions.normal} ${tokens.transitions.easing.default}`;
  element.style.transform = 'translateY(-4px)';
  
  setTimeout(() => {
    element.style.transform = 'translateY(0)';
  }, parseInt(tokens.transitions.normal));
}
```

### React/TypeScript Integration

```typescript
// useTokens.ts - Custom React hook
import { tokens, type Tokens } from './tokens';
import { useMemo } from 'react';

export function useTokens() {
  return useMemo(() => tokens, []);
}

// Component example
import React from 'react';
import { useTokens } from './hooks/useTokens';

export const FeatureCard: React.FC<{ title: string; description: string }> = ({ 
  title, 
  description 
}) => {
  const t = useTokens();
  
  return (
    <div 
      className="card"
      data-halo="2"
      style={{
        padding: t.spacing.lg,
        borderRadius: t.radius.lg,
      }}
    >
      <h3 style={{ color: t.colors.neutral.text.primary }}>
        {title}
      </h3>
      <p style={{ color: t.colors.neutral.text.secondary }}>
        {description}
      </p>
    </div>
  );
};
```

---

## File Structure

```
src/
├── lib/
│   ├── styles.css          # Main stylesheet with CSS variables and global styles
│   ├── tokens.ts           # TypeScript design tokens
│   └── components/         # Svelte components with scoped styles
│       ├── Header.svelte
│       ├── Hero.svelte
│       ├── FeatureGrid.svelte
│       └── ...
├── routes/
│   └── +layout.svelte      # Imports styles.css globally
└── app.html                # HTML template with font imports

static/
├── print.css               # Print-specific styles
└── ...
```

---

## ESP/Pi Adoption Status

### Current Token Usage Audit

The LumiGrid system spans multiple platforms: SvelteKit landing page, ESP32 LED Nodes, and Raspberry Pi gateways. This section documents the adoption status of design tokens across these platforms.

#### SvelteKit Landing Page ✓

**Status:** Fully adopted

All components in the landing page use the design token system via:
- CSS custom properties from `src/lib/styles.css`
- TypeScript tokens from `src/lib/tokens.ts`
- Consistent application of glassmorphism, halo effects, and motion system

**Components using tokens:**
- Header.svelte
- Hero.svelte
- All card-based components
- Interactive demos (LightSettings.svelte, etc.)
- Form elements and buttons

#### ESP32 LED Node Web UI

**Status:** Partial adoption recommended

The ESP32 nodes serve their web UI from flash memory with size constraints. A minimal token subset is recommended:

**Currently Used (embedded in C strings):**
```c
// Core color tokens
--bg-0, --bg-1, --ink, --muted
--accent-primary
--cya, --mag, --amb, --cherry

// Glass effects
--glass-bg, --glass-border

// Motion (respecting reduced motion)
--dur-med, --ease-default
```

**Recommended additions:**
- `--fs-body`, `--fs-caption` for consistent typography
- `--spacing-mobile-sm`, `--spacing-mobile-md` for touch-friendly layouts
- `--touch-target-min` for accessibility

**Implementation guide:**
1. Store minimal CSS token subset in `esp-idf/main/web/styles.min.css`
2. Inline critical styles in HTTP response headers
3. Use aggressive minification to fit flash constraints
4. Reference full StyleSystem.md for token values

**Example minimal ESP token set:**
```c
static const char* ESP_MINIMAL_TOKENS = 
":root{"
"--bg-0:#000;"
"--ink:#f5f5f7;"
"--muted:#a1a1a6;"
"--accent-primary:#0071e3;"
"--glass-bg:rgba(29,29,31,.72);"
"--dur-med:300ms;"
"--ease-default:cubic-bezier(.4,0,.2,1);"
"}"
"@media(prefers-reduced-motion:reduce){:root{--dur-med:0ms;}}";
```

#### Raspberry Pi Gateway/Hub Node

**Status:** Full adoption possible

Raspberry Pi nodes have sufficient resources for complete token adoption:

**Recommended approach:**
1. Use Flask/Jinja2 templates with full `lumigrid-styles.css`
2. Serve static CSS file from `/static/css/`
3. Import tokens in Python for dynamic theming:

```python
# tokens.py
TOKENS = {
    'colors': {
        'brand': {
            'primary': '#0071e3',
            'ledCyan': '#00d4ff',
            # ... etc
        }
    }
}

# In Flask routes
@app.route('/dashboard')
def dashboard():
    return render_template('dashboard.html', tokens=TOKENS)
```

**Benefits:**
- Consistent design language across platforms
- Theme switching capabilities
- Easy customization for different installations

### Migration Checklist

For ensuring complete token adoption across platforms:

- [x] SvelteKit landing page using full token system
- [ ] ESP32 web UI updated with minimal token subset
- [ ] Raspberry Pi gateway using full token system
- [ ] Documentation includes ESP-IDF code examples
- [ ] Documentation includes Flask/Python examples
- [ ] Cross-platform token value consistency verified
- [ ] Mobile-first responsive tokens applied to all platforms

### Token Sync Workflow

To maintain consistency when updating tokens:

1. **Update source of truth:** Modify `src/lib/tokens.ts` and `src/lib/styles.css`
2. **Regenerate ESP subset:** Run script to extract minimal token set
3. **Update Pi styles:** Copy updated `lumigrid-styles.css` to Pi static assets
4. **Update documentation:** Reflect changes in StyleSystem.md
5. **Test across platforms:** Verify visual consistency

### Platform-Specific Considerations

| Platform | Token Format | Size Constraint | Adoption Level |
|----------|-------------|-----------------|----------------|
| **SvelteKit** | CSS Custom Properties + TS | None | Full (100%) |
| **ESP32** | Inlined CSS strings | ~4KB flash | Minimal subset (30%) |
| **Raspberry Pi** | CSS file + Python dict | None | Full recommended (100%) |
| **C/Native** | Compile-time constants | Varies | Manual extraction |

---

## Best Practices

1. **Use CSS Custom Properties**: Always reference design tokens via CSS variables for consistency.

2. **Mobile-First**: Design for mobile first, then enhance for larger screens.

3. **Reduced Motion**: Always respect `prefers-reduced-motion` for animations.

4. **Touch Targets**: Ensure all interactive elements are at least 44px × 44px.

5. **Focus States**: Provide visible focus indicators for keyboard navigation.

6. **Glassmorphism Performance**: Use `content-visibility: auto` and `contain` for performance.

7. **Color Contrast**: Maintain WCAG AA contrast ratios for text.

8. **Semantic HTML**: Use appropriate HTML elements (buttons for actions, links for navigation).

9. **Component Isolation**: Keep component-specific styles scoped within Svelte components.

10. **Animation Performance**: Use `transform` and `opacity` for smooth animations.
