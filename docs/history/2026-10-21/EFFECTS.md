> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# Effects & Scene Engine

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

The LumiGrid effect system is a full scene-driven lighting engine capable of multi-layered blending and timeline sequences.

## Scene Model

The runtime supports a rich scene model containing:
- 32 discrete presets (scenes)
- 8 output zones
- 3 programmable layers per zone
- 4 distinct blend modes
- Global scene properties (brightness, transition times)
- Overlay/finishing effects (e.g. Strobe) applied on top of the base scene.

## Available Dynamic Effects

The node includes a library of dynamic effects, not just static animations. Effects are retrieved dynamically from the API (`GET /api/v1/effects`), allowing UIs to construct controls without hardcoded assumptions.

Implemented effects include:
- Solid
- Breathe
- Chase
- Sweep
- Reveal
- Sparkle
- Gradient
- Strobe
- Fire
- Aurora
- ColorWave
- StaticPulse

Effect parameters include `label`, `description`, `parameter label`, `unit`, `min`, `max`, and `default` to drive dynamic UI rendering.

## Timelines & Scheduling

The system includes a 16-step sequencer (timeline) across the 16 physical channels (A1–A8 addressable, P1–P8 PWM).
Calendar-based scheduling allows automated preset and sequence activation based on days-of-week and minute-of-day.
