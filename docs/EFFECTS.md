# Effects and playback

Read `/api/v1/effects` and `/api/v1/palettes` for the current metadata. Twelve effect IDs are implemented:

| ID | Label |
| --- | --- |
| solid | Steady color |
| breathe | Smooth pulse |
| chase | Moving comet |
| sweep | Color sweep |
| reveal | Soft reveal |
| sparkle | Random twinkle |
| gradient | Flowing glow |
| strobe | Beat strobe |
| fire | Living flame |
| aurora | Aurora glow |
| colorwave | Color wave |
| staticpulse | Static pulse |

Metadata defines speed, secondary-color/palette support, spatial behavior, PWM suitability and parameter bounds. Spatial effects cannot create spatial detail on one PWM dimmer.

## Storage and playback

There are 32 preset slots, eight zones and three layers per zone. Blend modes: normal, add, multiply, screen. Scene motion supports four tracks of six points. Timelines expose 16 steps; cue storage supports eight lists of 16 cues. These are model capacities, not throughput benchmarks.

Scenes, Timeline and Cues dashboard pages author these documents. APIs separate storage, activation, motion, sequence and transport control. See [API reference](API_REFERENCE.md).

Priority, highest first: EStop, Factory, LiveDmx, ManualOverride, Scheduler, ScenePlayback, Fallback, Idle. Live input can override a saved scene; releasing manual control is different from saving a preset.

`SceneManager::init()` loads the saved active scene, tries slot zero if unavailable, then defaults to blackout if neither loads. It does not promise exact cue/timeline-position restoration after power loss. Setup, safe mode, configuration failures and output faults can prevent playback.

Calendar operation uses the configured timezone and clock service. Validate clock readiness and schedules on the installation.

Sources: `LumigridLedNode/components/config/include/SignageTypes.hpp`, `signage/EffectRenderer.cpp`, `SceneManager.cpp`, `AutomationEngine.cpp`, `include/ControlSource.hpp`, `scheduler/`, and `show/`.
