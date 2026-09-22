# LumiGrid Next Phase — Operator Platform v2

Baseline: `bdbf4187c420e0fa9f5e263c893d1ee17dbda2a5`.

This phase is deliberately **integration-first**. The runtime/lifecycle architecture is frozen unless a concrete defect requires change. The objective is to make the existing firmware and dashboard behave like one coherent lighting-control product before adding another large feature family.

## Reference patterns

The direction follows proven patterns from mature/open-source lighting projects:

- WLED: keep the device API explicit and automation-friendly; expose presets/effects/segments without making the UI the source of truth.
- WLEDashboard: treat fleet state, groups/scenes, schedules, and live previews as first-class operator concepts.
- ASLS Studio: separate fixture/group state from scene composition, timeline sequencing, and visualisation.
- Aurora: use HTTP for commands and a push channel for live state propagation.

These references are inspiration for architecture and UX, not code dependencies.

## Target architecture

```text
Svelte Operator Console
        |
        +-- typed HTTP client ----> /api/v1/* ----> Firmware services
        |
        +-- WebSocket state ------> live snapshot

Firmware
  FactoryApi
    |-- scenes / effects
    |-- timeline / schedules
    |-- diagnostics / health
    |-- factory / burn-in
    |-- configuration / recovery
    `-- OTA
          |
    Control arbitration
          |
    LightingRuntime
          |
    OutputController
```

## Phase order

### 1. API consolidation

- Make `/api/v1/*` the canonical external-dashboard contract.
- Contain legacy `/json/*` calls behind a compatibility boundary.
- Split the TypeScript client by domain instead of growing one `node.ts` file indefinitely.
- Standardise error objects and validation messages.
- Verify response-size budgets for presets, health, logs, backup, and WebSocket snapshots.

### 2. Session and authorization

- Replace browser-only PIN gating with node-backed sessions/tokens.
- Keep development credentials simple/hardcoded for now.
- UI roles: operator / installer / admin.
- Firmware remains the authority for authorization.

### 3. Live state

- WebSocket is read-only state propagation.
- HTTP remains the command path.
- One frontend node-state store feeds Fleet, Scenes, Factory, Diagnostics, and Visualizer.
- Explicit connection states: offline / connecting / online / degraded.

### 4. Scene Studio

- Visual scene preview.
- Zone/layer editing.
- Effect metadata drives controls dynamically.
- Save / Save As / duplicate / activate.
- Clear dirty/saved/error states.

### 5. Timeline Studio

- Replace form-like editing with a visual beat grid.
- Drag/resize/duplicate steps.
- BPM-aware editing.
- Multi-track/zone view.
- Preview against the same scene model used by the device.

### 6. Diagnostics + Factory

- Diagnostics becomes an actionable engineering instrument, not a raw metric dump.
- Factory becomes the standard bench/manufacturing test console.
- Burn-in gets elapsed/remaining time, live health, abort, and final result.

### 7. Configuration + recovery

- Structured configuration editor.
- Explicit persistence state.
- Backup/restore validation and recovery UI.
- OTA progress/reboot/reconnect state.

### 8. Hardware validation

Execute the documented matrix: boot, networking, API, WebSocket, pixel output, PCA9685, DMX, arbitration, limiter, E-stop, watchdog, recovery, backup/restore, OTA, and extended burn-in.

## Explicit non-goals for this phase

- No new node families.
- No broad runtime rewrite.
- No cloud service.
- No proliferation of new effects before the existing scene/effect system is excellent.
- No second independent dashboard architecture.

## Definition of done

A new operator can discover/add a node, see its real live state, compose a scene, preview it, activate it, sequence it, schedule it, diagnose a fault, run a factory test, and recover/update the node without needing to understand firmware internals.
