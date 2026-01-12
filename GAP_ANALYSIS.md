# LumiGrid Gap Analysis

This document outlines the findings from the analysis of the LumiGrid repository structure, API consistency, and functionality gaps.

## 1. Repository Structure

### Current State
The repository consists of four distinct sub-projects located in the root directory:
- `lumigrid-lander-main/`: SvelteKit-based landing page and documentation.
- `lumigrid_display_node-main/`: Python/Flask-based application for Raspberry Pi LED matrices.
- `lumigrid_led_node-main/`: ESP-IDF/C firmware for ESP32-based LED controllers.
- `lumigrid_misc-main/`: Shared schemas, examples, and tools.

### Issues
- **Lack of Root Management**: Before this analysis, there was no unified way to install dependencies or run tests across projects.
- **Loose Coupling**: Sub-projects act as independent silos rather than a cohesive monorepo.

### Improvements Made
- Added a `Makefile` to the root directory to standardize `install`, `test`, and `clean` commands.

## 2. API Consistency

### Presets API
There was a significant divergence between the JSON schemas in `lumigrid_misc-main` and the actual implementations in the nodes.

#### Display Node (`lumigrid_display_node-main`)
- **Implementation**: Uses a `frame_config` object with high-level parameters like `type` (time, text), `font`, `position`, `colors`.
- **Previous Schema**: Defined `frame_data` as a raw array of pixel RGB values.
- **Action**: Updated `lumigrid_misc-main/schemas/preset-display.schema.json` to match the actual high-level configuration format.

#### LED Node (`lumigrid_led_node-main`)
- **Implementation**: Uses `effect_id`, `speed`, `intensity`, and palette/color arrays.
- **Previous Schema**: Was missing key fields and validation constraints.
- **Action**: Updated `lumigrid_misc-main/schemas/preset-led.schema.json` to match the `api_spec.yaml` and firmware implementation.

### Status API
Both nodes expose `/api/status`, but the response structures differ slightly in their metrics objects. This is acceptable given the hardware differences (Temp/CPU for Pi vs Heap/FPS for ESP32).

## 3. Frontend & UI

### Technology Stack Divergence
- **Lander & LED Node**: Use **SvelteKit**. This is the modern, reactive stack chosen for the project.
- **Display Node**: Uses **Flask Templates** (Jinja2). This is a legacy server-side rendering approach.

### Recommendations
- **Unify UI Stack**: The Display Node UI should eventually be rewritten in SvelteKit to share components (Design System) with the Lander and LED Node.
- **Shared Design System**: The `StyleSystem.md` in `lumigrid-lander-main` should be the source of truth. The Display Node's CSS should be audited to ensure it uses the same CSS variables/tokens.

## 4. Testing

### Coverage
- **Display Node**: Has a `tests/` folder with pytest support.
- **LED Node**: Has `idf.py test` support for firmware components.
- **Lander**: Has `vitest` configured.
- **Integration**: **MISSING**. There are no end-to-end tests that verify communication between a Display Node and an LED Node (e.g., sensor data sync, UDP sync).

### Recommendations
- Create an integration test suite (possibly in `lumigrid_misc-main` or a new `tests/` folder at root) that spins up dockerized versions of the nodes and asserts on their interaction.

## 5. Documentation

- The `lumigrid_misc-main/schemas` are now the source of truth for the data structures.
- The root `README.md` should be updated to guide new developers on how to use the `Makefile` and navigate the repo.
