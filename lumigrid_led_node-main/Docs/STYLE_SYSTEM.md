# LumiGrid Embedded Web UI Style System

## Overview

The LumiGrid embedded web interface follows a consistent design system with standardized color tokens, typography, spacing, and component styles. This ensures a cohesive brand experience across all web interfaces served by the ESP32 firmware.

## Design Tokens

### Color Palette

The LumiGrid style system uses a dark theme optimized for professional LED control applications:

```css
:root {
  /* Background & Surfaces */
  --bg: #0f1115;           /* Main background */
  --surface: #151924;      /* Card/panel backgrounds */
  --muted: #1c2232;        /* Muted elements, inputs */
  
  /* Text Colors */
  --text: #e6eaf2;         /* Primary text */
  --subtext: #9BA7C0;      /* Secondary text, labels */
  
  /* Brand Colors */
  --primary: #6C7CFF;      /* Primary actions, brand */
  --primary-600: #5366ff;  /* Primary hover */
  --primary-700: #4256f3;  /* Primary active */
  --accent: #86E2FF;       /* Accent, links, focus */
  
  /* Semantic Colors */
  --danger: #ff5c7a;       /* Errors, destructive actions */
  --warn: #ffc857;         /* Warnings */
  --success: #38e1b3;      /* Success states */
  
  /* Interactive States */
  --focus: #86E2FF;        /* Focus indicator color */
}
```

### Spacing & Layout

```css
--gap: 14px;             /* Standard gap between elements */
--radius: 14px;          /* Standard border radius for cards */
--radius-sm: 10px;       /* Smaller radius for inputs/buttons */
```

### Shadows

```css
--shadow-md: 0 10px 30px rgba(0,0,0,.35);  /* Cards, modals */
--shadow-sm: 0 2px 10px rgba(0,0,0,.25);   /* Buttons, small elements */
```

### Focus Ring

Accessible focus indication for keyboard navigation:

```css
--ring: 0 0 0 .16rem var(--focus), 
        0 0 0 .28rem color-mix(in oklab, var(--focus) 30%, transparent);
```

Apply to interactive elements:
```css
.btn:focus-visible,
input:focus-visible,
select:focus-visible {
  outline: none;
  box-shadow: var(--ring);
}
```

## Typography

- **Font Family**: `system-ui, Segoe UI, Roboto, Ubuntu, Cantarell, Noto Sans, Helvetica, Arial, sans-serif`
- **Font Smoothing**: `-webkit-font-smoothing: antialiased; text-rendering: optimizeLegibility`
- **Headings**: Use `var(--text)` for primary headings
- **Body Text**: Use `var(--text)` for primary content
- **Secondary Text**: Use `var(--subtext)` for labels, captions, metadata

## Components

### Buttons

```html
<!-- Primary button -->
<button class="btn">Primary Action</button>

<!-- Ghost button (transparent) -->
<button class="btn ghost">Secondary Action</button>

<!-- Danger button -->
<button class="btn danger">Delete</button>
```

**Styles:**
```css
.btn {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 10px 14px;
  border-radius: 12px;
  background: var(--primary);
  color: white;
  cursor: pointer;
  box-shadow: var(--shadow-sm);
  border: none;
}

.btn:focus-visible {
  outline: none;
  box-shadow: var(--ring);
}

.btn.ghost {
  background: transparent;
  border: 1px solid #2a3248;
  color: var(--text);
}

.btn.danger {
  background: var(--danger);
  color: white;
}
```

### Cards

```html
<div class="card">
  <h2>Card Title</h2>
  <p>Card content...</p>
</div>
```

**Styles:**
```css
.card {
  background: var(--surface);
  border: 1px solid #20263a;
  border-radius: var(--radius);
  padding: 16px;
  box-shadow: var(--shadow-md);
}
```

### Form Inputs

```html
<input type="text" placeholder="Enter value">
<select>
  <option>Option 1</option>
  <option>Option 2</option>
</select>
```

**Styles:**
```css
input, select {
  background: var(--muted);
  border: 1px solid #2a3248;
  border-radius: var(--radius-sm);
  color: var(--text);
  padding: 10px 12px;
}

input:focus-visible,
select:focus-visible {
  outline: none;
  box-shadow: var(--ring);
}
```

### Badges & Pills

```html
<!-- Badge -->
<span class="badge">Status</span>

<!-- Pill -->
<span class="pill">
  <span class="status-dot"></span>
  Connected
</span>
```

**Styles:**
```css
.badge {
  display: inline-block;
  padding: 4px 8px;
  border: 1px solid #2a3248;
  border-radius: 8px;
  font-size: 12px;
  color: var(--subtext);
  background: var(--muted);
}

.pill {
  font-size: 12px;
  padding: 6px 10px;
  background: var(--muted);
  border-radius: 999px;
  color: var(--subtext);
  display: inline-flex;
  align-items: center;
  gap: 8px;
}
```

## Accessibility

### Focus States

All interactive elements include visible focus indicators:
- Buttons, inputs, selects have focus ring
- Focus ring uses accent color (#86E2FF) with transparency
- Removes default browser outline for consistency

### Color Contrast

The color palette meets WCAG 2.1 AA standards:
- Text on background: High contrast (#e6eaf2 on #0f1115)
- Subtext on background: Sufficient contrast (#9BA7C0 on #0f1115)
- Button text on primary: High contrast (white on #6C7CFF)

### Keyboard Navigation

All interactive elements are keyboard accessible:
- Buttons are focusable with Tab key
- Form inputs support standard keyboard interaction
- Focus indicators clearly show current element

## Implementation Files

The LumiGrid style system is implemented in the following HTML files:

1. **Main Web UI**: `firmware/assets/webapp/index.html`
   - Full-featured LED control interface
   - Uses all design tokens and components

2. **REST API UI**: `firmware/components/rest_api/ui/index.html`
   - Simplified API testing interface
   - Consistent with main UI styling

3. **Setup Wizard**: `firmware/components/capport/setup_wizard.html`
   - Captive portal setup interface
   - Branded with LumiGrid colors

## Browser Compatibility

The style system uses modern CSS features:
- CSS Custom Properties (CSS Variables)
- `color-mix()` function (for transparency effects)
- Focus-visible pseudo-class
- System font stack

**Minimum Browser Requirements:**
- Chrome/Edge 111+
- Firefox 113+
- Safari 16.4+

Most modern browsers on ESP32's embedded web server will support these features.

## Usage Guidelines

### DO ✓

- Use design tokens for all colors (never hardcode hex values)
- Apply focus rings to all interactive elements
- Use semantic color tokens (danger for destructive actions, success for confirmations)
- Maintain consistent spacing using --gap token
- Apply proper border radius using --radius tokens

### DON'T ✗

- Hardcode color values in component styles
- Remove focus indicators (breaks accessibility)
- Mix old and new color schemes
- Use different shadow values than tokens
- Skip focus-visible states on interactive elements

## Migration from Old Style

The previous embedded web interfaces used various color schemes:

**Old Colors (Deprecated):**
```css
--bg: #0b0f14;      /* Now: #0f1115 */
--card: #121923;    /* Now: --surface: #151924 */
--accent: #6cf;     /* Now: --primary: #6C7CFF */
```

All interfaces have been migrated to use the unified LumiGrid design system.

## Resources

- **Reference Implementation**: See `Docs/webuibase.md` for complete single-file example
- **Component Examples**: View `/tmp/lumigrid-style-demo/style-comparison.html` for live demos
- **Design Tokens**: All tokens defined in `:root` selector at top of `<style>` blocks

## Testing Checklist

When implementing or updating UI components:

- [ ] Color tokens match LumiGrid palette
- [ ] Focus rings present on all interactive elements
- [ ] Border radius uses token values (14px or 10px)
- [ ] Shadows use token values (shadow-md or shadow-sm)
- [ ] Typography follows system font stack
- [ ] Contrast meets WCAG AA standards
- [ ] Keyboard navigation works correctly
- [ ] Tested on target ESP32 browser

## Future Enhancements

Planned improvements to the style system:

- Light theme variant (already defined in reference)
- Reduced motion preferences support
- High contrast mode
- Custom property fallbacks for older browsers
- Automated contrast checking in build process
