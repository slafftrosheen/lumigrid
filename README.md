# LumiGrid

LumiGrid is a comprehensive ecosystem for modular, intelligent lighting and display systems. It consists of synchronized nodes (LED strips, Matrix displays) controlled via a unified architecture.

## Repository Structure

This repository contains the following sub-projects:

*   **[lumigrid-lander-main/](lumigrid-lander-main/)**: SvelteKit-based landing page, documentation, and design system.
*   **[lumigrid_display_node-main/](lumigrid_display_node-main/)**: Python/Flask application for Raspberry Pi-based LED Matrix displays.
*   **[lumigrid_led_node-main/](lumigrid_led_node-main/)**: ESP-IDF firmware for ESP32-based addressable LED controllers.
*   **[lumigrid_misc-main/](lumigrid_misc-main/)**: Shared JSON schemas, API examples, and tools.

## Getting Started

A `Makefile` is provided to help manage the sub-projects.

### Prerequisites
*   Node.js (v20+)
*   Python (3.9+)
*   ESP-IDF (v5.3+) - for firmware development

### Common Commands

```bash
# Install dependencies for all sub-projects
make install

# Run tests for all sub-projects
make test

# Clean build artifacts
make clean
```

### Documentation

*   **[Gap Analysis](GAP_ANALYSIS.md)**: Report on API consistency and current system gaps.
*   **[Design System](lumigrid-lander-main/StyleSystem.md)**: UI/UX guidelines.
*   **[API Schemas](lumigrid_misc-main/schemas/)**: JSON Schemas for data interchange.

## Development

Each sub-project has its own `README.md` with specific development instructions. Please refer to them for detailed setup guides.
