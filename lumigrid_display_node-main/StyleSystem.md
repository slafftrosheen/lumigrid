# LumiGrid Style System

A comprehensive documentation of the styling system for the LumiGrid ecosystem. This system is inspired by Apple's design language with vibrant LED brand colors and is applied consistently across:

- **Landing Page** - Marketing and product showcase (Svelte/TypeScript)
- **Pi Display Node Web UI** - Device control interface (Flask/Jinja2/HTML)

Both implementations share the same design tokens, color palette, typography, and component patterns for a unified brand experience.

---

## Quick Rebranding Guide

Want to customize the look and feel? Jump to:
- [Simple Color Changes](#simple-color-changes) - Change colors in minutes
- [Typography Customization](#typography-customization-guide) - Use your fonts
- [Component Styling](#component-styling-examples) - Modify specific components
- [Complete Rebrand](#complete-rebranding-workflow) - Full custom theme

---

## Implementation Notes

### Landing Page
The design token system is defined in `src/lib/tokens.ts` and provides a JavaScript/TypeScript interface for the design system. The actual styling implementation uses CSS custom properties defined in `src/lib/styles.css`.

### Pi Display Node
The web UI at `lumigrid/web/static/css/style.css` implements these same design tokens using CSS custom properties. The Flask/Jinja2 templates in `lumigrid/web/templates/` use the component classes defined in this system.

---

## Table of Contents

1. [Design Tokens](#design-tokens)
2. [CSS Custom Properties](#css-custom-properties)
3. [Color System](#color-system)
4. [Typography](#typography)
5. [Spacing](#spacing)
6. [Effects & Animations](#effects--animations)
7. [Components](#components)
8. [Responsive Design](#responsive-design)
9. [Accessibility](#accessibility)
10. [Print Styles](#print-styles)
11. [Customization Guides](#customization-guides)

---

## Design Tokens

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

---

## Customization Guides

### Simple Color Changes

The easiest way to rebrand is to change the color palette. All colors are defined using CSS custom properties.

**Location**: `lumigrid/web/static/css/style.css`

#### Change Accent Color

Replace the primary accent color (default: Apple blue):

```css
:root {
  --accent-primary: #FF6B00;    /* Change to your brand color */
  --ring: var(--accent-primary);
}
```

#### Change LED Brand Colors

Customize the LED-inspired colors:

```css
:root {
  /* Replace with your brand colors */
  --mag: #9D00FF;        /* Purple instead of magenta */
  --cherry: #FF1744;     /* Bright red */
  --cya: #00E5FF;        /* Lighter cyan */
  --amb: #FFC107;        /* Golden amber */
}
```

#### Change Background Colors

Adjust the dark theme backgrounds:

```css
:root {
  --bg-0: #121212;       /* Slightly lighter black */
  --bg-1: #1E1E1E;       /* Dark gray */
  --bg: #0A0A0A;         /* Almost black */
}
```

#### Change Text Colors

Modify text colors while maintaining readability:

```css
:root {
  --ink: #FFFFFF;        /* Pure white text */
  --muted: #AAAAAA;      /* Lighter muted text */
}
```

### Typography Customization Guide

#### Use Custom Fonts

1. **Add your font files** to `lumigrid/web/static/fonts/`

2. **Define font-face** in `style.css`:

```css
@font-face {
  font-family: 'MyCustomFont';
  src: url('../fonts/MyCustomFont.woff2') format('woff2'),
       url('../fonts/MyCustomFont.woff') format('woff');
  font-weight: 400;
  font-style: normal;
  font-display: swap;
}
```

3. **Update font variables**:

```css
:root {
  --font-display: 'MyCustomFont', -apple-system, sans-serif;
  --font-body: 'MyCustomFont', system-ui, sans-serif;
}
```

#### Adjust Font Sizes

Modify the typography scale:

```css
:root {
  /* Make everything slightly larger */
  --fs-display-xl: clamp(3.5rem, 9vw, 7rem);
  --fs-display-lg: clamp(2.5rem, 6vw, 4rem);
  --fs-title-1: clamp(2rem, 5vw, 3rem);
  --fs-body: 1.125rem;  /* 18px instead of 16px */
}
```

### Component Styling Examples

#### Customize Buttons

Make buttons more rounded and colorful:

```css
.btn {
  border-radius: 2rem;  /* More rounded */
  padding: 1rem 1.5rem;
  font-weight: 700;     /* Bolder */
}

.btn.primary {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  box-shadow: 0 8px 32px rgba(102, 126, 234, 0.4);
}

.btn.primary:hover {
  transform: translateY(-4px);
  box-shadow: 0 12px 48px rgba(102, 126, 234, 0.6);
}
```

#### Customize Cards

Change card appearance:

```css
.card {
  background: #1a1a2e;  /* Dark blue background */
  border: 2px solid #16213e;
  border-radius: 24px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
}

.card:hover {
  border-color: #0f3460;
  transform: translateY(-4px);
  box-shadow: 0 16px 48px rgba(0, 0, 0, 0.4);
}
```

#### Customize Navigation Header

Style the header differently:

```css
.header {
  background: rgba(26, 26, 46, 0.95) !important;
  backdrop-filter: saturate(180%) blur(30px);
  border-bottom: 1px solid rgba(102, 126, 234, 0.3);
}

.header::after {
  background: linear-gradient(90deg,
    rgba(102, 126, 234, 0.3),
    rgba(118, 75, 162, 0.3));
}
```

### Complete Rebranding Workflow

Follow these steps for a complete rebrand:

#### Step 1: Define Your Brand Colors

Create a color palette:

```css
:root {
  /* Primary brand color */
  --brand-primary: #FF6B00;
  --brand-primary-dark: #CC5500;
  --brand-primary-light: #FF8533;
  
  /* Secondary colors */
  --brand-secondary: #00BFA6;
  --brand-accent: #FFD600;
  
  /* Backgrounds */
  --brand-bg-dark: #1A1A2E;
  --brand-bg-medium: #16213E;
  --brand-bg-light: #0F3460;
  
  /* Text */
  --brand-text-primary: #EAEAEA;
  --brand-text-secondary: #A0A0A0;
}
```

#### Step 2: Apply Brand Colors

Map brand colors to functional tokens:

```css
:root {
  /* Map brand colors to system tokens */
  --accent-primary: var(--brand-primary);
  --bg-0: var(--brand-bg-dark);
  --bg-1: var(--brand-bg-medium);
  --bg: var(--brand-bg-dark);
  --ink: var(--brand-text-primary);
  --muted: var(--brand-text-secondary);
  
  /* Update gradients */
  --primary-grad: linear-gradient(135deg, 
    var(--brand-primary), 
    var(--brand-secondary));
}
```

#### Step 3: Customize Typography

```css
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap');

:root {
  --font-display: 'Inter', -apple-system, sans-serif;
  --font-body: 'Inter', system-ui, sans-serif;
  --fw-bold: 700;
}
```

#### Step 4: Adjust Components

Update button styles:

```css
.btn.primary {
  background: var(--brand-primary);
  color: white;
}

.btn.primary:hover {
  background: var(--brand-primary-dark);
}
```

Update card styles:

```css
.card {
  background: var(--brand-bg-medium);
  border-color: var(--brand-bg-light);
}
```

#### Step 5: Test and Refine

1. **Test on different devices** - Mobile, tablet, desktop
2. **Check accessibility** - Contrast ratios, readability
3. **Test dark/light modes** - If applicable
4. **Verify animations** - Smooth transitions
5. **Review print styles** - If needed

### Advanced Customization

#### Create Custom Gradients

```css
:root {
  --custom-gradient-1: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  --custom-gradient-2: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
  --custom-gradient-3: radial-gradient(circle at top right, #667eea, transparent);
}

/* Apply to elements */
.hero {
  background: var(--custom-gradient-1);
}

.card.featured::before {
  background: var(--custom-gradient-3);
}
```

#### Create Custom Halo Effects

```css
[data-halo="custom"] {
  --halo-primary: #667eea;
  --halo-secondary: #764ba2;
  --halo-glow: #f093fb;
  --halo-amount: 0.20;
}

[data-halo="custom"]::before {
  background:
    radial-gradient(circle at 20% 20%, var(--halo-primary) 0%, transparent 50%),
    radial-gradient(circle at 80% 60%, var(--halo-secondary) 0%, transparent 50%),
    radial-gradient(circle at 50% 50%, var(--halo-glow) 0%, transparent 60%);
}
```

#### Custom Animation Speeds

```css
:root {
  /* Faster animations */
  --dur-fast: 100ms;
  --dur-med: 200ms;
  --dur-slow: 350ms;
  
  /* Or slower, more relaxed */
  --dur-fast: 200ms;
  --dur-med: 400ms;
  --dur-slow: 600ms;
}
```

### Rebranding Checklist

- [ ] Define brand color palette
- [ ] Update CSS custom properties
- [ ] Choose and import fonts
- [ ] Update typography variables
- [ ] Customize button styles
- [ ] Customize card styles
- [ ] Update header/navigation
- [ ] Adjust gradients and effects
- [ ] Test on multiple devices
- [ ] Check color contrast (WCAG AA)
- [ ] Verify animations work smoothly
- [ ] Test print styles if needed
- [ ] Document custom changes

### Example: Gaming Theme

Complete example of a gaming-themed rebrand:

```css
/* Gaming theme colors */
:root {
  /* Neon gaming colors */
  --accent-primary: #00ff41;
  --mag: #ff006e;
  --cherry: #fb5607;
  --cya: #8338ec;
  --amb: #ffbe0b;
  
  /* Dark cyberpunk backgrounds */
  --bg-0: #0d0d0d;
  --bg-1: #1a1a2e;
  --bg: #0a0a0a;
  
  /* Bright text */
  --ink: #00ff41;
  --muted: #39ff14;
  
  /* Neon glow gradient */
  --primary-grad: linear-gradient(135deg, #00ff41, #8338ec, #ff006e);
  
  /* Gaming font */
  --font-display: 'Orbitron', 'Rajdhani', monospace;
  --font-body: 'Rajdhani', system-ui, sans-serif;
}

/* Neon button effect */
.btn.primary {
  background: transparent;
  border: 2px solid var(--accent-primary);
  color: var(--accent-primary);
  text-shadow: 0 0 10px var(--accent-primary);
  box-shadow: 0 0 20px rgba(0, 255, 65, 0.3);
}

.btn.primary:hover {
  background: var(--accent-primary);
  color: #000;
  text-shadow: none;
  box-shadow: 0 0 30px rgba(0, 255, 65, 0.6),
              inset 0 0 20px rgba(0, 255, 65, 0.3);
}

/* Neon card glow */
.card {
  background: var(--bg-1);
  border: 1px solid rgba(0, 255, 65, 0.3);
  box-shadow: 0 0 20px rgba(0, 255, 65, 0.1),
              inset 0 0 10px rgba(0, 255, 65, 0.05);
}

.card:hover {
  border-color: rgba(0, 255, 65, 0.6);
  box-shadow: 0 0 40px rgba(0, 255, 65, 0.3),
              inset 0 0 20px rgba(0, 255, 65, 0.1);
}
```

### Example: Corporate Theme

Professional corporate rebrand:

```css
/* Corporate theme */
:root {
  /* Professional blues */
  --accent-primary: #0052CC;
  --brand-secondary: #003D99;
  --brand-accent: #00B8D9;
  
  /* Clean backgrounds */
  --bg-0: #FFFFFF;
  --bg-1: #F4F5F7;
  --bg: #FAFBFC;
  
  /* Dark text for light theme */
  --ink: #172B4D;
  --muted: #5E6C84;
  
  /* Subtle gradient */
  --primary-grad: linear-gradient(135deg, #0052CC, #00B8D9);
  
  /* Professional fonts */
  --font-display: 'Inter', 'Segoe UI', sans-serif;
  --font-body: 'Inter', system-ui, sans-serif;
}

/* Clean professional buttons */
.btn.primary {
  background: var(--accent-primary);
  color: white;
  border-radius: 4px;
  padding: 0.75rem 1.5rem;
  font-weight: 600;
  box-shadow: 0 1px 3px rgba(0, 82, 204, 0.2);
}

.btn.primary:hover {
  background: var(--brand-secondary);
  box-shadow: 0 4px 8px rgba(0, 82, 204, 0.3);
}

/* Clean cards */
.card {
  background: white;
  border: 1px solid #DFE1E6;
  border-radius: 8px;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.05);
}

.card:hover {
  border-color: #B3BAC5;
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
}
```

---

## Troubleshooting Customizations

### Colors Not Changing

**Problem**: Custom colors don't apply

**Solution**:
1. Check CSS specificity - use `!important` if needed
2. Clear browser cache
3. Verify CSS file is loaded
4. Check for typos in variable names

### Fonts Not Loading

**Problem**: Custom fonts don't appear

**Solution**:
1. Verify font files are in correct directory
2. Check @font-face syntax
3. Ensure font-display is set
4. Check browser dev tools for 404 errors
5. Use web-safe fallback fonts

### Animations Broken

**Problem**: Animations don't work

**Solution**:
1. Check `prefers-reduced-motion` setting
2. Verify animation keyframes are defined
3. Check animation timing values
4. Test in different browsers

### Layout Issues

**Problem**: Layout breaks on mobile

**Solution**:
1. Test at different viewport sizes
2. Check media query breakpoints
3. Verify flexbox/grid properties
4. Use browser dev tools responsive mode

---

## Resources

### Design Tools

- **Color Palette Generators**:
  - [Coolors](https://coolors.co/)
  - [Adobe Color](https://color.adobe.com/)
  
- **Typography**:
  - [Google Fonts](https://fonts.google.com/)
  - [Font Pair](https://www.fontpair.co/)
  
- **Accessibility**:
  - [WebAIM Contrast Checker](https://webaim.org/resources/contrastchecker/)
  - [WAVE Tool](https://wave.webaim.org/)

### Learning Resources

- [MDN CSS Documentation](https://developer.mozilla.org/en-US/docs/Web/CSS)
- [CSS Tricks](https://css-tricks.com/)
- [Web.dev](https://web.dev/learn/css/)

---

## Getting Help

For styling questions or issues:

- **GitHub Discussions**: https://github.com/algorhythmicdev/lumigrid_display_node/discussions
- **Issues**: https://github.com/algorhythmicdev/lumigrid_display_node/issues

---

**Happy customizing!** 🎨
