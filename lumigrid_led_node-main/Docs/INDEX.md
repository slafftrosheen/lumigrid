# LumiGrid LED Node - Documentation Index

**Complete Documentation Overview**  
**Version:** 1.0.0  
**Last Updated:** 2026-01-10

---

## 📖 Documentation Structure

This repository contains comprehensive documentation organized into focused guides. This index helps you find the right information quickly.

---

## 🚀 Start Here

### For New Users
1. **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)** — Quick start and common tasks
2. **[SETUP_GUIDE.md](SETUP_GUIDE.md)** — Complete setup from scratch
3. **[API_REFERENCE.md](API_REFERENCE.md)** — REST API usage

### For Integrators
1. **[API_REFERENCE.md](API_REFERENCE.md)** — REST API endpoints and examples
2. **[NETWORKING_GUIDE.md](NETWORKING_GUIDE.md)** — Wi-Fi, MQTT, UDP sync
3. **[API_EXAMPLES.md](API_EXAMPLES.md)** — Code examples in multiple languages

### For Hardware Builders
1. **[HARDWARE_GUIDE.md](HARDWARE_GUIDE.md)** — Wiring, safety, specifications
2. **[SETUP_GUIDE.md](SETUP_GUIDE.md)** — Software setup
3. **[QUICK_START.md](QUICK_START.md)** — Fast setup for experienced users

### For Developers
1. **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** — Contributing and coding standards
2. **[ARCHITECTURE.md](ARCHITECTURE.md)** — System design
3. **[api_spec.yaml](api_spec.yaml)** — OpenAPI specification

---

## 📚 Complete Guide List

### Essential Guides

| Guide | Description | Key Topics |
|-------|-------------|------------|
| **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)** | Quick reference and common tasks | Commands, API endpoints, troubleshooting |
| **[SETUP_GUIDE.md](SETUP_GUIDE.md)** | Complete setup and installation | ESP-IDF, building, flashing, first boot |
| **[API_REFERENCE.md](API_REFERENCE.md)** | REST API documentation | Endpoints, schemas, examples |
| **[HARDWARE_GUIDE.md](HARDWARE_GUIDE.md)** | Hardware setup and safety | Wiring, pinout, power, safety |
| **[NETWORKING_GUIDE.md](NETWORKING_GUIDE.md)** | Network configuration | Wi-Fi, MQTT, UDP sync |
| **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** | Troubleshooting guide | Common issues, diagnostics, solutions |
| **[DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)** | Contributing and development | Coding standards, testing, security |

### Architecture & Design

| Document | Description |
|----------|-------------|
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | System architecture and design |

### API Documentation

| Document | Description |
|----------|-------------|
| **[api_spec.yaml](api_spec.yaml)** | OpenAPI specification |
| **[API_EXAMPLES.md](API_EXAMPLES.md)** | Additional code examples |
| **[PRESET_API.md](PRESET_API.md)** | Preset management API |
| **[PWM_EFFECTS_API.md](PWM_EFFECTS_API.md)** | PWM effects API |
| **[SEQUENCER_API.md](SEQUENCER_API.md)** | Sequencer API |

### Specialized Topics

| Document | Description |
|----------|-------------|
| **[OTA_GUIDE.md](OTA_GUIDE.md)** | Over-the-air firmware updates |
| **[OTA_UI_GUIDE.md](OTA_UI_GUIDE.md)** | OTA UI integration |
| **[PRESET_VALIDATION_EXAMPLES.md](PRESET_VALIDATION_EXAMPLES.md)** | Preset validation examples |
| **[PRESET_SECURITY.md](PRESET_SECURITY.md)** | Preset security considerations |
| **[STYLE_SYSTEM.md](STYLE_SYSTEM.md)** | UI style system |
| **[STYLE_SYSTEM_IMPLEMENTATION.md](STYLE_SYSTEM_IMPLEMENTATION.md)** | Style implementation details |

---

## 🎯 Documentation by Topic

### Setup & Installation
- [SETUP_GUIDE.md](SETUP_GUIDE.md) — Complete setup guide
- [QUICK_START.md](QUICK_START.md) — Fast setup
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — Common issues & solutions

### Hardware
- [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md) — Wiring and specifications
- [board_pinmap.h](board_pinmap.h) — GPIO pin definitions

### Networking
- [NETWORKING_GUIDE.md](NETWORKING_GUIDE.md) — Wi-Fi, MQTT, sync protocol

### REST API
- [API_REFERENCE.md](API_REFERENCE.md) — Complete API documentation
- [API_EXAMPLES.md](API_EXAMPLES.md) — Code examples
- [api_spec.yaml](api_spec.yaml) — OpenAPI specification

### Effects & Presets
- [PRESET_API.md](PRESET_API.md) — Preset management
- [PRESET_VALIDATION_EXAMPLES.md](PRESET_VALIDATION_EXAMPLES.md) — Validation examples

### Development
- [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) — Contributing guidelines
- [ARCHITECTURE.md](ARCHITECTURE.md) — System architecture

### Security
- [PRESET_SECURITY.md](PRESET_SECURITY.md) — Preset security
- [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) — Development security

### OTA Updates
- [OTA_GUIDE.md](OTA_GUIDE.md) — OTA update guide
- [OTA_UI_GUIDE.md](OTA_UI_GUIDE.md) — OTA UI integration

---

## 📦 Additional Resources

### Schemas
- [preset-led.schema.json](../firmware/schemas/preset-led.schema.json) — Preset validation schema

### Configuration Examples
- [default_config.json](default_config.json) — Default node configuration

---

## 🔍 Finding Information

### By Question

**"How do I set up the device?"**
→ [SETUP_GUIDE.md](SETUP_GUIDE.md)

**"What are the API endpoints?"**
→ [API_REFERENCE.md](API_REFERENCE.md) or [QUICK_REFERENCE.md](QUICK_REFERENCE.md)

**"How do I wire the hardware?"**
→ [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md)

**"How do I configure Wi-Fi/MQTT?"**
→ [NETWORKING_GUIDE.md](NETWORKING_GUIDE.md)

**"How do I contribute code?"**
→ [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md)

**"How does the system work?"**
→ [ARCHITECTURE.md](ARCHITECTURE.md)

**"How do I create presets?"**
→ [PRESET_API.md](PRESET_API.md)

**"What are the safety considerations?"**
→ [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md)

**"How do I update firmware?"**
→ [OTA_GUIDE.md](OTA_GUIDE.md)

**"Something isn't working, how do I fix it?"**
→ [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

---

## 🌐 External Resources

- **ESP-IDF Documentation:** https://docs.espressif.com/projects/esp-idf/en/latest/
- **ESP32 Datasheet:** https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
- **WS2812B Datasheet:** https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf
- **PCA9685 Datasheet:** https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
- **GitHub Repository:** https://github.com/algorhythmicdev/lumigrid_led_node

---

## ✉️ Support

- **GitHub Issues:** https://github.com/algorhythmicdev/lumigrid_led_node/issues

---

**Documentation Index Version:** 1.0  
**Last Updated:** 2026-01-10
