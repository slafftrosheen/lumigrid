# LumiGrid Component Development Checklist

Use this checklist when creating or modifying components to ensure consistency with the LumiGrid StyleSystem.

---

## Pre-Development

- [ ] Review [StyleSystem.md](StyleSystem.md) for applicable patterns
- [ ] Check [STYLE_AUDIT.md](STYLE_AUDIT.md) for current best practices
- [ ] Identify required design tokens from `src/lib/tokens.ts`
- [ ] Verify component doesn't duplicate existing functionality

---

## Styling

### Color & Theming
- [ ] Use CSS custom properties for all colors (e.g., `var(--ink)`, `var(--accent-primary)`)
- [ ] Never hard-code color hex values
- [ ] Support light mode if applicable (`@media (prefers-color-scheme: light)`)
- [ ] Use LED brand colors appropriately (`var(--cya)`, `var(--mag)`, `var(--amb)`, `var(--cherry)`)
- [ ] Apply glass effects with `var(--glass-bg)` and `var(--glass-border)`

### Typography
- [ ] Use font size tokens: `var(--fs-h1)`, `var(--fs-body)`, etc.
- [ ] Apply font families: `var(--font-display)` for headings, `var(--font-body)` for text
- [ ] Use `clamp()` for fluid typography: `clamp(min, ideal, max)`
- [ ] Set `text-wrap: balance` for headings
- [ ] Use appropriate font weights: `var(--fw-regular)`, `var(--fw-semibold)`, etc.

### Spacing & Layout
- [ ] Use spacing tokens or `clamp()` for responsive spacing
- [ ] Apply mobile spacing tokens: `var(--spacing-mobile-sm)`, etc.
- [ ] Use standard grid classes: `.grid`, `.grid-2`, `.grid.three`
- [ ] Implement mobile-first responsive design
- [ ] Use `min()` for max-width constraints: `width: min(1200px, 92vw)`

### Components
- [ ] Use `.card` class for content surfaces
- [ ] Apply `data-halo` attribute for ambient effects: `data-halo="2"`
- [ ] Use `.btn` classes: `.btn.primary`, `.btn.outline`, `.btn.ghost`
- [ ] Apply `.grad-frame` for animated borders
- [ ] Use `.pill` or `.chip` for toggles/filters
- [ ] Add `.reveal` class for scroll animations

### Motion & Transitions
- [ ] Use motion tokens: `var(--dur-fast)`, `var(--dur-med)`, `var(--dur-slow)`
- [ ] Apply easing functions: `var(--ease-out)`, `var(--ease-default)`
- [ ] Support reduced motion: Animations automatically disabled via tokens
- [ ] Use `transform` and `opacity` for performant animations
- [ ] Avoid animating layout properties (width, height, top, left)

---

## Accessibility

### Semantic HTML
- [ ] Use appropriate semantic elements (`<button>`, `<nav>`, `<main>`, etc.)
- [ ] Use `<button>` for actions, `<a>` for navigation
- [ ] Provide proper heading hierarchy (h1 → h2 → h3)
- [ ] Include `aria-label` for icon-only buttons
- [ ] Use `role` attributes when needed

### Focus & Interaction
- [ ] Ensure all interactive elements are keyboard accessible
- [ ] Focus states use `var(--accent-primary)` outline (handled globally)
- [ ] Touch targets are minimum 44px: `min-height: var(--touch-target-min)`
- [ ] Hover effects don't interfere with accessibility
- [ ] Focus-visible states are distinct from hover

### Screen Readers
- [ ] Include descriptive alt text for images
- [ ] Use `aria-hidden="true"` for decorative elements
- [ ] Provide `aria-label` or `aria-labelledby` for complex widgets
- [ ] Use `role="status"` or `aria-live` for dynamic updates
- [ ] Test with screen reader (NVDA, JAWS, VoiceOver)

### Color Contrast
- [ ] Text meets WCAG AA contrast ratio (4.5:1 for body, 3:1 for large text)
- [ ] Don't rely on color alone to convey information
- [ ] Interactive states have sufficient contrast
- [ ] Test with color blindness simulator

---

## Responsive Design

### Breakpoints
- [ ] Mobile-first: Start with mobile styles, enhance for larger screens
- [ ] Test at key breakpoints: 480px, 768px, 1024px
- [ ] Use media queries defined in StyleSystem
- [ ] Ensure touch-friendly on mobile (44px targets)

### Mobile Optimization
- [ ] Reduce animation complexity on mobile
- [ ] Optimize halo effects for mobile performance
- [ ] Use smaller blur values on mobile: `@media (max-width: 768px)`
- [ ] Stack layouts vertically on small screens
- [ ] Test on actual mobile devices, not just browser resize

### Performance
- [ ] Use `will-change` sparingly and only during interaction
- [ ] Apply `contain: content` and `content-visibility: auto` for cards
- [ ] Lazy load images with `loading="lazy"`
- [ ] Minimize layout shifts during animations
- [ ] Test with Chrome DevTools Performance tab

---

## Code Quality

### Svelte Best Practices
- [ ] Use TypeScript types where applicable
- [ ] Keep components focused (single responsibility)
- [ ] Extract reusable logic to `$lib/` utilities
- [ ] Use reactive statements (`$:`) for derived state
- [ ] Avoid unnecessary reactivity

### CSS Organization
- [ ] Scope styles within `<style>` blocks
- [ ] Use CSS custom properties for dynamic values
- [ ] Avoid `!important` unless absolutely necessary
- [ ] Group related styles together
- [ ] Comment complex CSS logic

### Token Usage
- [ ] Import tokens from `$lib/tokens` for dynamic styling
- [ ] Use CSS custom properties for static styling
- [ ] Never duplicate token definitions
- [ ] Document any new tokens added

---

## Testing

### Visual Testing
- [ ] Test in Chrome, Firefox, Safari
- [ ] Test on iOS Safari and Chrome Mobile
- [ ] Verify at multiple viewport sizes
- [ ] Check light mode (if supported)
- [ ] Test with browser zoom at 200%

### Functional Testing
- [ ] All interactive elements work as expected
- [ ] Keyboard navigation works correctly
- [ ] Tab order is logical
- [ ] Forms validate properly
- [ ] No console errors or warnings

### Accessibility Testing
- [ ] Run axe DevTools or Lighthouse accessibility audit
- [ ] Test keyboard navigation
- [ ] Test with screen reader
- [ ] Verify reduced-motion mode
- [ ] Check color contrast with tools

### Performance Testing
- [ ] Check Lighthouse performance score
- [ ] Verify no layout shifts (CLS)
- [ ] Monitor animation frame rate (60fps target)
- [ ] Test on low-end mobile devices
- [ ] Check bundle size impact

---

## Documentation

### Code Comments
- [ ] Add JSDoc comments for exported functions
- [ ] Document complex logic or workarounds
- [ ] Explain non-obvious CSS techniques
- [ ] Include usage examples for reusable components

### Component Documentation
- [ ] Add component to relevant section if creating new patterns
- [ ] Document props and their types
- [ ] Provide usage examples
- [ ] Note accessibility considerations
- [ ] Link to related components

### StyleSystem Updates
- [ ] Add new tokens to `tokens.ts` and `styles.css`
- [ ] Document new tokens in StyleSystem.md
- [ ] Update token reference table
- [ ] Add usage examples
- [ ] Update cross-platform guides if applicable

---

## Pre-Commit

### Code Review
- [ ] Run `npm run check` (Svelte type checking)
- [ ] Run `npm run lint:copy` (copy quality check)
- [ ] Fix all TypeScript warnings
- [ ] Review git diff for unintended changes
- [ ] Remove console.log and debug code

### Final Checks
- [ ] Component works in isolation
- [ ] No breaking changes to existing components
- [ ] Bundle size impact is acceptable
- [ ] Performance is within acceptable range
- [ ] All checklist items addressed

---

## Post-Merge

### Monitoring
- [ ] Verify component in production
- [ ] Monitor for accessibility issues
- [ ] Check analytics for interaction metrics
- [ ] Gather user feedback
- [ ] Document lessons learned

### Maintenance
- [ ] Add component to automated visual regression tests
- [ ] Update Storybook/showcase if exists
- [ ] Share component patterns with team
- [ ] Consider extracting for reuse across platforms

---

## Common Patterns Reference

### Standard Card
```svelte
<div class="card" data-halo="2">
  <h3>Title</h3>
  <p>Description with proper color: var(--muted)</p>
  <button class="btn primary">Action</button>
</div>
```

### Responsive Grid
```svelte
<div class="grid two">
  <div class="card">Item 1</div>
  <div class="card">Item 2</div>
</div>

<style>
  @media (max-width: 768px) {
    /* Grid automatically stacks on mobile via global styles */
  }
</style>
```

### Button Group
```svelte
<div class="hero-cta" style="display: flex; gap: 1rem; flex-wrap: wrap;">
  <button class="btn primary">Primary</button>
  <button class="btn outline">Secondary</button>
</div>
```

### Dynamic Token Usage
```svelte
<script>
  import { tokens } from '$lib/tokens';
  
  function getStatusColor(status) {
    return status === 'active' 
      ? tokens.colors.brand.ledCyan 
      : tokens.colors.neutral.text.secondary;
  }
</script>

<div style="color: {getStatusColor(status)}">
  Status indicator
</div>
```

### Reveal Animation
```svelte
<section class="section container reveal">
  <h2>Animated on Scroll</h2>
  <p>Content that reveals smoothly</p>
</section>
```

---

## Resources

- **[StyleSystem.md](StyleSystem.md)** — Complete design system documentation
- **[STYLE_AUDIT.md](STYLE_AUDIT.md)** — Audit report and migration guide
- **[src/lib/tokens.ts](src/lib/tokens.ts)** — TypeScript design tokens
- **[src/lib/styles.css](src/lib/styles.css)** — CSS custom properties
- **[WCAG Guidelines](https://www.w3.org/WAI/WCAG21/quickref/)** — Accessibility standards
- **[MDN Web Docs](https://developer.mozilla.org/)** — HTML/CSS/JavaScript reference

---

**Last Updated:** 2026-01-08  
**Version:** 1.0.0
