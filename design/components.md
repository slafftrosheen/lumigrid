# Component Design Guidelines

> Design guidance, not an implementation or hardware acceptance report. Check the active Svelte components and [current firmware documentation](../docs/README.md) for implemented behavior.

## The LumiGrid Aesthetic

LumiGrid is a lighting engine. Its interface should reflect light.

### Glassmorphism (Use with Hierarchy)

Glass effects should be used strictly for **hierarchy**, not decoration.
Do not turn every container into a generic "glass box".

Correct usage:
- `Background` (pure black / very dark gray)
  - `Section / Container` (subtle glass border/bg)
    - `Interactive Control` (solid glass or distinct element)
      - `Active State` (illuminated)

### Light as Identity

The strongest visual identity should come from **light itself**.
Instead of using generic UI styles, use live light previews. For example, a scene card should not just be text and a glass background; it should include a visual representation of the scene's colors and effects.

```text
┌────────────────────────────┐
│ ●●●●●●●●●●●●●●●●●●●●●●●●   │
│                            │
│ Evening Ambient            │
│ Warm / Breathe             │
│                            │
│ 8 zones             72%    │
└────────────────────────────┘
```

This ensures the interface feels specific to LumiGrid, rather than feeling like a generic dashboard.
