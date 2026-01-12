# LumiGrid

LumiGrid is a comprehensive ecosystem for modular, intelligent lighting and display systems. It consists of synchronized nodes (LED strips, Matrix displays) controlled via a unified architecture.

## Repository Structure

This repository contains the following sub-projects:

*   **[lumigrid-lander-main/](lumigrid-lander-main/)**: SvelteKit-based landing page, documentation, and design system.
*   **[lumigrid_display_node-main/](lumigrid_display_node-main/)**: Python/Flask application for Raspberry Pi-based LED Matrix displays.
*   **[lumigrid_led_node-main/](lumigrid_led_node-main/)**: ESP-IDF firmware for ESP32-based addressable LED controllers.
*   **[lumigrid_misc-main/](lumigrid_misc-main/)**: Shared JSON schemas, API examples, and tools.

## Universal Onboarding

This monorepo uses a root `Makefile` to standardize the setup and development process for all subprojects. Whether you are a frontend developer, a Python scripter, or a firmware engineer, your onboarding journey starts here.

### 1. Install Dependencies

The `make install` command installs all necessary dependencies for every subproject, including `npm` packages and Python libraries.

```bash
make install
```

### 2. Run Tests

The `make test` command runs all available tests for the subprojects to ensure that everything is working correctly.

```bash
make test
```

### 3. Explore Subprojects

Once the initial setup is complete, you can explore the subproject directories for more specific development tasks. Each subproject's `README.md` file contains detailed instructions that build on this universal onboarding process.

### Documentation

*   **[Gap Analysis](GAP_ANALYSIS.md)**: Report on API consistency and current system gaps.
*   **[Design System](lumigrid-lander-main/StyleSystem.md)**: UI/UX guidelines.
*   **[API Schemas](lumigrid_misc-main/schemas/)**: JSON Schemas for data interchange.

## Development

Each sub-project has its own `README.md` with specific development instructions. Please refer to them for detailed setup guides.
