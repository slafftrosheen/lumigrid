# Effects and playback

The firmware catalogue contains **24 effects and 14 palettes**. Read `GET /api/v1/effects` and `GET /api/v1/palettes` from the connected node for its supported catalogue; an older firmware version may expose fewer entries. Effect metadata defines the label, description, speed control, secondary-color and palette support, spatial behavior, PWM recommendation, and parameter limits/defaults.

The effect IDs below are stable API names. Existing numeric effect IDs 0–11 and palette IDs 0–5 retain their identities; new entries are appended. Saved scenes continue to refer to the same effects and palettes.

## Choosing an effect

Addressable WS2812 RGB and SK6812 RGBW lanes can display position, color, and brightness changes across up to 256 pixels. Each of the eight PWM outputs is an independent single-channel dimmer. It can reproduce a brightness rhythm, but it cannot display a color gradient or a moving spot.

**PWM: yes** in the tables means the effect is recommended by the firmware metadata for a single dimmer. **PWM: no** means use an addressable lane for its intended appearance. In particular, the new spatial and color-only effects emit steady PWM duty; choosing them on a dimmer does not reproduce their pixel animation. Color wave also has a dimmer envelope, but remains marked as an addressable effect because its intended appearance is spatial.

For speed-enabled effects, `speedMs` is a timing duration: a larger value produces slower motion or change. It is the complete cycle for most repeating patterns. Candlelight, Living flame, Soft twinkle, and Confetti use it as a noise or event timescale. Random twinkle has a fixed 50 ms update interval and does not expose a speed control.

The parameter column lists the supported range followed by the default in parentheses. These are effect parameters, separate from layer opacity, scene brightness, and output brightness limits.

### Established effects

| API ID | Label | Appearance and useful distinction | Parameter: range (default) | PWM |
| --- | --- | --- | --- | --- |
| `solid` | Steady color | An even wash of the primary color. | None | Yes |
| `breathe` | Smooth pulse | Brightness rises and falls smoothly while the selected color blend or palette changes with the pulse. | Minimum glow: 1–100% (4%) | Yes |
| `chase` | Moving comet | A bright head travels from the beginning to the end of the lane, followed by a short fading tail. | Tail length: 1–8 pixels (3) | No |
| `sweep` | Color sweep | The primary color progressively fills the lane, then starts again. | None | No |
| `reveal` | Soft reveal | The whole lane fades in together, holds, and repeats. It does not sequence separate lanes. | Fade length: 10–90% of the cycle (25%) | Yes |
| `sparkle` | Random twinkle | Crisp white accents appear over a steady primary-color background every 50 ms. | Twinkles: 1–12 per lane (2) | No |
| `gradient` | Flowing glow | A moving color gradient carries a soft brightness wave. Lower contrast makes the intensity more even. | Wave contrast: 0–100% (100%) | No |
| `strobe` | Beat strobe | The primary color switches on and off with a regular duty cycle. | Flash time: 5–95% of the cycle (50%) | Yes |
| `fire` | Living flame | A flickering color texture with a configurable height profile along the lane. | Flame height: 20–100% (70%) | No |
| `aurora` | Aurora glow | The primary color's RGB components gently change across the whole lane together. | Color depth: 1–255 (120) | Yes |
| `colorwave` | Color wave | A repeated sine-shaped color and brightness wave moves along the pixels. | Wave length: 10–255 pixels (50) | No |
| `staticpulse` | Static pulse | A smooth brightness pulse that keeps the primary color fixed. | Min dim: 0–100% (20%) | Yes |

### Additional effects

| API ID | Label | Appearance and useful distinction | Parameter: range (default) | PWM |
| --- | --- | --- | --- | --- |
| `rainbow` | Rainbow flow | A full spectrum moves along the lane at even intensity. It uses its own rainbow colors, independent of the palette selector. | Spectrum repeats: 1–8 (1) | No |
| `colorfade` | Color fade | The whole lane blends through the selected palette and back, holding at each endpoint without an added brightness pulse. | Endpoint hold: 0–40% of the cycle at each end (10%) | No |
| `scanner` | Soft scanner | A soft spotlight moves to the far end and back, with fractional pixel coverage for smooth motion. | Spot width: 1–64 pixels (6) | No |
| `theater` | Marquee chase | Spaced lights advance continuously along the lane. | Spacing: 2–32 pixels (6) | No |
| `twinkle` | Soft twinkle | Colored points gently fade in and out over black. Each pixel has a repeatable time offset. | Density: 1–100% (25%) | No |
| `candle` | Candlelight | An even primary-color wash follows an irregular, smoothly changing brightness rhythm. Depth zero gives a steady wash. | Flicker depth: 0–100% (35%) | Yes |
| `heartbeat` | Heartbeat | A stronger pulse, a softer second pulse, and a quiet rest repeat as one cycle. | Minimum glow: 0–80% (4%) | Yes |
| `ripple` | Center ripple | Soft fronts expand from the center toward both ends and disappear beyond the lane. | Ring width: 1–64 pixels (8) | No |
| `dissolve` | Dissolve | Pixels transition between palette endpoints in a repeatable shuffled order, then reverse. Choose a black endpoint for a fill-and-clear appearance. | Edge softness: 1–100% (15%) | No |
| `plasma` | Plasma flow | Several overlapping waves create a moving color texture. Increasing scale adds spatial detail. | Scale: 1–8 waves (3) | No |
| `lightning` | Lightning | A short cluster of primary-color flashes has varying flash strength, followed by a dark half-cycle. | Flashes: 1–5 per burst (3) | Yes |
| `confetti` | Confetti | Colored sparks appear abruptly over black, then decay into softer glows. | Density: 1–100% (15%) | No |

Density controls the proportion of eligible pixel events, not an exact number of lit pixels in every frame. The difference between the sparkle choices is deliberate: Random twinkle adds hard white accents to a lit background, Soft twinkle fades colored points in and out, and Confetti uses a fast appearance followed by a fade.

On a one-pixel addressable lane, color and brightness effects still have useful behavior, while spatial movement is reduced to the value at that pixel. A one-pixel Soft scanner is steady because its start and end positions coincide. Choose Candlelight, Heartbeat, Smooth pulse, or Static pulse when the aim is to animate a single dimmer.

## Palettes and colors

Palette-capable effects are Smooth pulse, Moving comet, Flowing glow, Living flame, Color wave, Color fade, Soft scanner, Marquee chase, Soft twinkle, Center ripple, Dissolve, Plasma flow, and Confetti. Other effects use the primary color or their own fixed color rule. The connected node's metadata controls which editor fields are available.

**Custom (0)** interpolates between the layer's primary and secondary colors. A named palette supplies its own colors, so editing the two custom colors does not replace that palette. Setting both custom colors to the same value gives palette-capable effects a single-color appearance; choosing black at one end allows effects such as Dissolve to move between color and darkness.

| Palette ID | Name | Color character |
| --- | --- | --- |
| 0 | Custom | The layer's primary-to-secondary blend. |
| 1 | Rainbow | A continuous green, red, blue, and green spectrum. |
| 2 | Ocean | Blue and teal through bright cyan. |
| 3 | Lava | Deep red through orange and pale warm tones. |
| 4 | Forest | Green through brighter yellow-green. |
| 5 | Sunset | The established red and warm blend continuing toward pale cyan. |
| 6 | Amber | Dark amber, gold, and soft warm highlights. |
| 7 | Ice | Deep blue through cyan to near-white. |
| 8 | Neon | Cyan, violet, pink, and vivid green. |
| 9 | Lavender | Deep violet through lavender to pale pink. |
| 10 | Candy | Crimson and rose through pink to warm cream. |
| 11 | Festive | Red, gold, green, and warm white. |
| 12 | Ember | Near-black and deep red through orange to gold. |
| 13 | Twilight | Deep violet and magenta through peach. |

Palettes 6–13 interpolate four fixed color stops. The original six palettes retain their established rendering math. Palettes describe RGB color; the existing output color pipeline handles RGBW conversion, gamma, brightness, and power limiting for the configured hardware.

## Building a scene

1. Configure the target channels, pixel type/order, and pixel count, then read their current capabilities. The scene editors identify all 16 outputs as addressable lanes A1–A8 and PWM dimmers P1–P8. See [Configuration](CONFIGURATION.md).
2. Open a scene and enable a layer on the target channel. Choose an effect suitable for that output. Set the primary color, or select Custom and a secondary color when the effect supports a blend.
3. Set speed and the effect's parameter. Begin with the catalogue default, then adjust it while observing the connected output. A wide spot or ring on a short lane can illuminate most of the lane at once.
4. Add another layer when needed and choose its blend mode and opacity. Normal blends over the lower layer; Add and Screen are useful for bright accents over a wash; Multiply can shape a lower layer with a brightness pattern. A black background behaves differently under each blend mode.
5. Save the scene to a preset slot and activate it through the normal playback controls. Saving a document and activating playback are separate operations. Inspect applied/failed status and actual output; a preview is not physical verification.

For PWM layers, the **Intensity** control adjusts the layer's opacity (0–255) and preserves its stored primary and secondary RGB colors. It controls the layer's contribution to the dimmer output. The effect's brightness envelope, blend mode, scene brightness, and channel limits still determine the final level.

These combinations are useful starting points:

| Aim | Suggested scene setup |
| --- | --- |
| Gentle ambient dimming | Candlelight on a PWM output, or Static pulse when a regular rhythm is preferred. |
| A restrained branded sign | A low-opacity Steady color base, plus Soft scanner in Add or Screen mode using the same custom primary and secondary colors. |
| Decorative stars | Soft twinkle with Lavender or Ice; use Add or Screen over a dim base layer. |
| A colorful moving texture | Plasma flow with Neon or Twilight; start at the default scale before adding detail. |
| A shuffled color reveal | Dissolve with Custom, one brand color, and one black endpoint. |

## Storage and playback

There are 32 preset slots and eight zones/groups. Current scene schema 2 stores up to three layers for each of the 16 output channels; the legacy scene schema stored content by zone. Blend modes are `normal`, `add`, `multiply`, and `screen`. Scene motion supports four tracks of six points. Timelines expose 16 steps; cue storage supports eight lists of 16 cues. These are model capacities, not throughput benchmarks.

Scenes, Timeline, and Cues dashboard pages author these documents. APIs separate storage, activation, motion, sequence, and transport control. See [API reference](API_REFERENCE.md).

Current scene-save requests use the 16-entry `channels` array. The dashboard serializers omit the redundant legacy `zones` mirror when sending that representation. `PUT /api/v1/presets` accepts JSON bodies up to **16 KiB (16,384 bytes)** for the channel layers and bounded motion data; runtime memory admission still applies. Larger scene bodies return HTTP 413 with `scene_payload_too_large`. Other handlers using the default JSON reader retain its **8 KiB (8,192 bytes)** limit. Use effect IDs from the connected catalogue and integer palette IDs; the current catalogue accepts palette IDs 0–13.

Priority, highest first: EStop, Factory, LiveDmx, ManualOverride, Scheduler, ScenePlayback, Fallback, Idle. Live input can override a saved scene; releasing manual control is different from saving a preset.

`SceneManager::init()` loads the saved active scene, tries slot zero if unavailable, then defaults to blackout if neither loads. It does not promise exact cue/timeline-position restoration after power loss. Setup, safe mode, configuration failures, and output faults can prevent playback.

Calendar operation uses the configured timezone and clock service. Validate clock readiness and schedules on the installation.

## Rendering and validation boundaries

The added effects compute their frame from the layer settings, pixel position, and time. They use fixed storage with no per-frame heap allocation or previous-frame history. The same inputs produce the same frame regardless of skipped frames or which other layer rendered first. This does not establish synchronization accuracy between separate nodes.

The animation timestamp remains a 32-bit millisecond value. It wraps after approximately 49.7 days, and a phase change at that boundary remains possible. The effect expansion does not widen that clock or alter the output drivers.

Host renderer checks are available through `python LumigridLedNode/scripts/test_effects.py` from the repository root. These checks exercise production rendering code on the host; they do not substitute for a complete ESP-IDF/PlatformIO build, target timing measurements, or physical RGB/RGBW/PWM validation. See [Development](CONTRIBUTING.md) and [Hardware validation](HARDWARE_VALIDATION.md) for the separate workflows.

Sources: `LumigridLedNode/components/config/include/SignageTypes.hpp`, `signage/EffectRenderer.cpp`, `signage/LightingRuntime.cpp`, `SceneManager.cpp`, `AutomationEngine.cpp`, `include/ControlSource.hpp`, `scheduler/`, and `show/`. Catalogue API handlers are in `LumigridLedNode/components/factory/FactoryApi.cpp`.
