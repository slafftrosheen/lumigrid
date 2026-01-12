# Contributing to LumiGrid Landing

Welcome to the LumiGrid landing repository! This guide will help you contribute to the SvelteKit landing page, extend the StyleSystem, and understand how the design tokens integrate with the broader LumiGrid ecosystem.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Development Workflow](#development-workflow)
3. [SvelteKit Architecture](#sveltekit-architecture)
4. [Extending the StyleSystem](#extending-the-stylesystem)
5. [Design Token System](#design-token-system)
6. [Component Guidelines](#component-guidelines)
7. [Testing](#testing)
8. [Accessibility](#accessibility)
9. [Performance](#performance)
10. [Code Style](#code-style)

## Getting Started

### Prerequisites

- **Node.js** 20.0.0 or higher
- **npm** (comes with Node.js)
- Basic understanding of Svelte/SvelteKit
- Familiarity with modern CSS and design tokens

### Initial Setup

```bash
# Clone the repository
git clone https://github.com/algorhythmicdev/lumigrid-lander.git
cd lumigrid-lander

# Install dependencies
npm install

# Start development server
npm run dev -- --open
```

The site will open at http://localhost:5173 with hot module replacement enabled.

### Available Commands

```bash
npm run dev          # Start dev server with HMR
npm run build        # Build static site for production
npm run preview      # Preview production build locally
npm run check        # Run type checking and linting
npm run test         # Run Playwright end-to-end tests
npm run test:unit    # Run Vitest unit tests
npm run sync:tokens  # Sync design tokens between TS and CSS
npm run lint:copy    # Lint copy text for consistency
```

## Development Workflow

### 1. Create a Branch

```bash
git checkout -b feature/your-feature-name
```

### 2. Make Changes

- Write code following our [code style guidelines](#code-style)
- Test your changes locally with `npm run dev`
- Ensure accessibility with keyboard navigation and screen readers
- Check responsive behavior on multiple screen sizes

### 3. Validate Changes

```bash
# Type check and lint
npm run check

# Run tests
npm run test:unit
npm run test

# Build to catch production issues
npm run build
```

### 4. Commit and Push

```bash
git add .
git commit -m "feat: add your feature description"
git push origin feature/your-feature-name
```

### 5. Create Pull Request

- Provide a clear description of changes
- Reference any related issues
- Include screenshots for UI changes
- Ensure all CI checks pass

## SvelteKit Architecture

### Project Structure

```
lumigrid-lander/
├── src/
│   ├── routes/
│   │   ├── +layout.svelte      # Global layout, FX canvas bootstrap
│   │   └── +page.svelte        # Landing page content
│   ├── lib/
│   │   ├── components/         # Reusable Svelte components
│   │   ├── styles.css          # Global styles and design tokens
│   │   ├── tokens.ts           # TypeScript design tokens
│   │   └── fx.bundle.js        # 3D effects, parallax, animations
│   └── app.html                # HTML template
├── static/                     # Static assets (images, icons, etc.)
├── tests/                      # Playwright E2E tests
└── svelte.config.js           # SvelteKit configuration
```

### Key Files Explained

#### `src/routes/+layout.svelte`
- Global layout wrapper for all pages
- Initializes background FX canvas
- Imports global styles
- Sets up theme and accessibility features

#### `src/routes/+page.svelte`
- Main landing page content
- Composed of reusable components
- Demonstrates design system usage

#### `src/lib/styles.css`
- CSS custom properties (design tokens)
- Component base styles (cards, buttons, grids)
- Responsive breakpoints
- Accessibility utilities

#### `src/lib/tokens.ts`
- TypeScript mirror of CSS tokens
- Programmatic access to design values
- Type-safe token usage in components

#### `src/lib/fx.bundle.js`
- Canvas-based background effects
- Parallax scrolling
- Interactive animations
- Respects `prefers-reduced-motion`

### Adding a New Page

1. Create route file: `src/routes/your-page/+page.svelte`
2. Add SEO metadata (optional): `src/routes/your-page/+page.ts`
3. Link from navigation in `src/lib/components/Header.svelte`

### Creating Components

Place new components in `src/lib/components/` and follow this pattern:

```svelte
<script lang="ts">
  import { tokens } from '$lib/tokens';
  
  export let title: string;
  export let variant: 'primary' | 'secondary' = 'primary';
</script>

<div class="my-component" class:variant-{variant}>
  <h3>{title}</h3>
  <slot />
</div>

<style>
  .my-component {
    background: var(--glass-bg);
    border-radius: var(--radius-lg);
    padding: var(--spacing-lg);
  }
  
  .variant-primary {
    border: 1px solid var(--accent-primary);
  }
</style>
```

## Extending the StyleSystem

The LumiGrid design system is built on a two-tier token architecture:

1. **CSS Custom Properties** in `src/lib/styles.css` — runtime theming, fastest rendering
2. **TypeScript Tokens** in `src/lib/tokens.ts` — type-safe programmatic access

### Adding a New Color

#### Step 1: Add to CSS (`src/lib/styles.css`)

```css
:root {
  /* Existing colors... */
  
  /* Your new color */
  --my-new-color: #ff6b6b;
}

/* Light mode override if needed */
@media (prefers-color-scheme: light) {
  :root {
    --my-new-color: #ee5a5a;
  }
}
```

#### Step 2: Add to TypeScript (`src/lib/tokens.ts`)

```typescript
export const tokens = {
  colors: {
    brand: {
      // Existing colors...
      myNewColor: '#ff6b6b',
    },
    // ...
  },
  // ...
};
```

#### Step 3: Run Token Sync

```bash
npm run sync:tokens
```

This ensures consistency between CSS and TypeScript tokens.

#### Step 4: Document in StyleSystem.md

Add your new color to the [Color System](StyleSystem.md#color-system) section with:
- Color name and hex value
- Usage guidelines
- Example implementation

### Adding a New Spacing Token

```css
/* src/lib/styles.css */
:root {
  --spacing-custom: 2.5rem;
}
```

```typescript
// src/lib/tokens.ts
export const tokens = {
  spacing: {
    // ...existing
    custom: '2.5rem',
  },
};
```

### Adding a New Component Style

1. **Define base styles in `styles.css`:**

```css
.my-new-component {
  position: relative;
  background: var(--glass-bg);
  backdrop-filter: var(--glass-blur);
  border: 1px solid var(--glass-border);
  border-radius: 1rem;
  padding: var(--spacing-lg);
  transition: transform var(--dur-med) var(--ease-default);
}

.my-new-component:hover {
  transform: translateY(-2px);
}
```

2. **Create component file:**

```svelte
<!-- src/lib/components/MyNewComponent.svelte -->
<script lang="ts">
  export let title: string;
</script>

<div class="my-new-component">
  <h4>{title}</h4>
  <slot />
</div>
```

3. **Document in StyleSystem.md** under Components section

### Halo System

Components can use the `data-halo` attribute for glow effects:

```html
<!-- No glow -->
<div class="card" data-halo="0">Content</div>

<!-- Subtle glow -->
<div class="card" data-halo="1">Content</div>

<!-- Soft glow -->
<div class="card" data-halo="2">Content</div>

<!-- Strong glow -->
<div class="card" data-halo="3">Content</div>

<!-- Vivid glow -->
<div class="card" data-halo="4">Content</div>
```

Halo intensity tokens in `:root`:
```css
--halo-0: 0;          /* none */
--halo-1: .06;        /* subtle */
--halo-2: .10;        /* soft */
--halo-3: .16;        /* strong */
--halo-4: .22;        /* vivid */
```

## Design Token System

### Token Categories

#### Colors
- **Brand**: Primary accent colors (`--accent-primary`, LED colors)
- **Neutral**: Backgrounds and text (`--bg`, `--ink`, `--muted`)
- **Glass**: Glassmorphism effects (`--glass-bg`, `--glass-border`)

#### Spacing
- **Scale**: `xs`, `sm`, `md`, `lg`, `xl`, `xxl`
- **Mobile**: Touch-friendly spacing (`--spacing-mobile-*`)

#### Typography
- **Font Families**: System fonts with SF Pro fallbacks
- **Font Sizes**: Fluid scales using `clamp()`
- **Font Weights**: 400 (regular) to 700 (bold)

#### Effects
- **Shadows**: Depth and elevation
- **Blur**: Glass and backdrop effects
- **Halos**: LED-inspired glows

#### Motion
- **Durations**: `--dur-fast`, `--dur-med`, `--dur-slow`
- **Easing**: `--ease-default`, `--ease-in`, `--ease-out`
- **Reduced Motion**: Automatically disabled when `prefers-reduced-motion: reduce`

### Using Tokens

#### In CSS

```css
.my-element {
  color: var(--ink);
  background: var(--glass-bg);
  padding: var(--spacing-lg);
  transition: transform var(--dur-med) var(--ease-out);
}
```

#### In Svelte (Programmatic)

```svelte
<script>
  import { tokens } from '$lib/tokens';
  
  $: dynamicPadding = tokens.spacing.lg;
</script>

<div style="padding: {dynamicPadding}">
  Content
</div>
```

#### In TypeScript

```typescript
import { tokens } from '$lib/tokens';

const primaryColor = tokens.colors.brand.primary;
const mediumSpacing = tokens.spacing.md;
```

## Component Guidelines

### Accessibility First

1. **Semantic HTML**: Use proper heading hierarchy, landmarks, lists
2. **Keyboard Navigation**: All interactive elements must be keyboard accessible
3. **Focus Indicators**: Visible focus states for all interactive elements
4. **ARIA Labels**: Provide labels for icon-only buttons and custom controls
5. **Color Contrast**: Maintain WCAG AA minimum (4.5:1 for text)
6. **Touch Targets**: Minimum 44×44px for mobile interactions

### Example Accessible Component

```svelte
<script lang="ts">
  export let label: string;
  export let onClick: () => void;
</script>

<button 
  class="btn primary"
  on:click={onClick}
  aria-label={label}
  style="min-height: var(--touch-target-min, 44px)"
>
  <slot />
</button>

<style>
  .btn:focus-visible {
    outline: 2px solid var(--accent-primary);
    outline-offset: 4px;
  }
</style>
```

### Responsive Design

Use mobile-first approach with fluid typography:

```css
.my-component {
  /* Mobile first */
  padding: var(--spacing-mobile-md);
  font-size: clamp(0.875rem, 3vw, 1rem);
}

/* Tablet and up */
@media (min-width: 768px) {
  .my-component {
    padding: var(--spacing-lg);
  }
}

/* Desktop and up */
@media (min-width: 1024px) {
  .my-component {
    padding: var(--spacing-xl);
  }
}
```

### Performance Best Practices

1. **Content Visibility**: Use `content-visibility: auto` for off-screen content
2. **Image Optimization**: Use WebP with fallbacks, appropriate sizes
3. **Lazy Loading**: Defer non-critical images and components
4. **Reduce Motion**: Respect `prefers-reduced-motion` preference
5. **Code Splitting**: Use dynamic imports for large components

Example:

```css
.card {
  contain: content;
  content-visibility: auto;
  contain-intrinsic-size: 400px 300px;
}
```

## Testing

### Unit Tests (Vitest)

Place unit tests alongside components with `.test.ts` or `.spec.ts` extension:

```typescript
// src/lib/components/Button.test.ts
import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/svelte';
import Button from './Button.svelte';

describe('Button', () => {
  it('renders with correct label', () => {
    const { getByText } = render(Button, { props: { label: 'Click me' } });
    expect(getByText('Click me')).toBeInTheDocument();
  });
});
```

Run with: `npm run test:unit`

### End-to-End Tests (Playwright)

Create E2E tests in `tests/` directory:

```typescript
// tests/landing.spec.ts
import { test, expect } from '@playwright/test';

test('landing page loads correctly', async ({ page }) => {
  await page.goto('/');
  await expect(page.locator('h1')).toBeVisible();
  await expect(page.locator('h1')).toContainText('LumiGrid');
});

test('navigation works', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('link', { name: 'Contact' }).click();
  await expect(page.locator('#contact')).toBeInViewport();
});
```

Run with: `npm run test`

## Accessibility

### Required Checks

Before submitting:

1. **Keyboard Navigation**: Tab through all interactive elements
2. **Screen Reader**: Test with NVDA/VoiceOver/JAWS
3. **Color Contrast**: Use browser DevTools to verify ratios
4. **Reduced Motion**: Test with `prefers-reduced-motion` enabled
5. **Zoom**: Test at 200% zoom level
6. **Mobile**: Test on real devices or simulators

### Accessibility Features

The landing page includes:

- **Read Aloud Toggle**: Double-click any text element to hear it spoken
- **Skip Links**: Jump to main content for keyboard users
- **Focus Management**: Logical tab order, visible focus indicators
- **Semantic Structure**: Proper heading hierarchy, landmarks
- **Alt Text**: All images have descriptive alternative text
- **Motion Controls**: Animations disabled for users who prefer reduced motion

## Performance

### Performance Targets

- **Lighthouse Score**: 90+ on all metrics
- **First Contentful Paint**: < 1.5s
- **Largest Contentful Paint**: < 2.5s
- **Total Blocking Time**: < 200ms
- **Cumulative Layout Shift**: < 0.1

### Optimization Techniques

1. **Static Generation**: Build-time rendering with `@sveltejs/adapter-static`
2. **Asset Optimization**: Compress images, use modern formats (WebP, AVIF)
3. **Code Splitting**: Dynamic imports for non-critical code
4. **CSS Optimization**: Critical CSS inline, defer non-critical
5. **Lazy Loading**: Images and components below the fold

### Measuring Performance

```bash
# Build for production
npm run build

# Preview and measure
npm run preview
# Open Chrome DevTools → Lighthouse → Run audit
```

## Code Style

### TypeScript

- Use strict mode
- Provide type annotations for props and functions
- Avoid `any` types; use `unknown` if type is truly unknown

### CSS

- Use design tokens (CSS custom properties) instead of hardcoded values
- Follow BEM-like naming for component-specific classes
- Mobile-first responsive design
- Group related properties (positioning, box model, visual, typography)

### Svelte

- One component per file
- Props first, then reactive statements, then functions
- Keep component logic simple; extract complex logic to utilities
- Use semantic HTML elements
- Prefer composition over inheritance

### Example Component Structure

```svelte
<script lang="ts">
  import { tokens } from '$lib/tokens';
  
  // Props
  export let title: string;
  export let variant: 'primary' | 'secondary' = 'primary';
  
  // Reactive statements
  $: borderColor = variant === 'primary' 
    ? tokens.colors.brand.primary 
    : tokens.colors.neutral.text.secondary;
  
  // Functions
  function handleClick() {
    console.log('Card clicked');
  }
</script>

<!-- Template -->
<article 
  class="feature-card" 
  class:variant-{variant}
  on:click={handleClick}
>
  <h3>{title}</h3>
  <slot />
</article>

<!-- Scoped styles -->
<style>
  .feature-card {
    /* Positioning */
    position: relative;
    
    /* Box model */
    padding: var(--spacing-lg);
    border: 1px solid var(--glass-border);
    border-radius: 1rem;
    
    /* Visual */
    background: var(--glass-bg);
    backdrop-filter: var(--glass-blur);
    
    /* Typography */
    color: var(--ink);
  }
  
  .variant-primary {
    border-color: var(--accent-primary);
  }
  
  h3 {
    margin: 0 0 var(--spacing-sm);
    font-size: var(--fs-title-2);
  }
</style>
```

### Git Commit Messages

Follow conventional commits:

- `feat:` New feature
- `fix:` Bug fix
- `docs:` Documentation changes
- `style:` Code style changes (formatting, no logic change)
- `refactor:` Code refactoring
- `test:` Adding or updating tests
- `chore:` Maintenance tasks

Examples:
```
feat: add interactive LED demo component
fix: correct mobile menu z-index conflict
docs: update StyleSystem color palette guide
style: format ContactForm with prettier
refactor: extract theme logic to composable
test: add E2E test for contact form flow
chore: update dependencies to latest versions
```

## Additional Resources

- [SvelteKit Documentation](https://kit.svelte.dev/docs)
- [Svelte Tutorial](https://svelte.dev/tutorial)
- [StyleSystem Reference](./StyleSystem.md)
- [Node Family Documentation](./docs/NODE_FAMILY.md)
- [API Reference](./docs/API_REFERENCE.md)
- [Embedded Integration Guide](./docs/EMBEDDED_INTEGRATION.md)

## Questions or Issues?

- Open an issue on GitHub
- Check existing documentation in `README.md` and `StyleSystem.md`
- Review closed issues for similar problems
- Reach out to the team at hello@lumigrid.dev

Thank you for contributing to LumiGrid! 🌟
