# LumiGrid Display Node - Documentation Index

Complete index of all documentation for the LumiGrid Display Node project.

## 📱 In-App Documentation (Web UI)

**NEW! Access comprehensive documentation directly from the web interface:**

- **Help & Documentation** (`/help`) - Complete user and developer guides
  - Available at: `http://your-pi-ip:5000/help`
  - Covers all aspects: hardware, setup, usage, troubleshooting, API, development
  - Fully responsive and accessible
  - No internet connection required (hosted locally)

- **FAQ** (`/faq`) - Frequently asked questions
  - Available at: `http://your-pi-ip:5000/faq`
  - Organized by category with searchable content
  
- **Setup Wizard** - Interactive first-run configuration
  - Automatically appears on first run
  - Guides through node type selection and configuration

👉 **Recommended for end users:** Start with the in-app documentation for the most accessible experience!

## 📚 Getting Started

### For New Users

1. **[QUICKSTART.md](QUICKSTART.md)** - Start here! Get up and running in minutes
   - Installation guide
   - First run instructions
   - Basic usage examples
   - Quick API examples

2. **[README.md](README.md)** - Project overview and feature list
   - Feature overview
   - Quick installation summary
   - Navigation to all docs

3. **[HARDWARE.md](HARDWARE.md)** - Hardware setup guide
   - Component requirements
   - Wiring diagrams
   - Power supply info
   - Sensor connections

## ⚙️ Configuration

4. **[CONFIGURATION.md](CONFIGURATION.md)** - Complete configuration reference
   - All config.yaml options
   - Best practices
   - Common configurations
   - Troubleshooting

5. **[config.yaml.example](config.yaml.example)** - Example configuration file
   - Fully commented
   - Ready to customize

## 🔌 Integration

6. **[INTEGRATION.md](INTEGRATION.md)** - ESP32 LED Node integration
   - Architecture overview
   - Preset schema alignment
   - UDP synchronization setup
   - Sensor data bridging
   - Code examples (ESP32, Arduino)

7. **[docs/UDP_SYNCHRONIZATION.md](docs/UDP_SYNCHRONIZATION.md)** - UDP sync protocol
   - Protocol specification
   - Master/slave configuration
   - Clock state management
   - Troubleshooting sync issues

## 🌐 API Documentation

8. **[docs/API_REFERENCE.md](docs/API_REFERENCE.md)** - Complete REST API reference
   - All endpoints documented
   - Request/response schemas
   - Integration examples
   - Error handling
   - Rate limiting

9. **[EXAMPLES.md](EXAMPLES.md)** - API usage examples
   - Python examples
   - JavaScript examples
   - curl examples
   - Integration patterns

## 🎨 Presets and Display

10. **[preset_examples/](preset_examples/)** - Sample preset files
    - [README.md](preset_examples/README.md) - How to use presets
    - **Time displays** - 5 clock/date presets
    - **Temperature displays** - 3 temperature presets
    - **Message displays** - 3 text/message presets

11. **[schemas/preset-display.schema.json](schemas/preset-display.schema.json)** - Preset JSON schema
    - Validation schema
    - Field definitions
    - Frame types

12. **[docs/PRESET_SCHEMA_VALIDATION.md](docs/PRESET_SCHEMA_VALIDATION.md)** - Schema validation guide

## ⏱️ Scheduling

13. **[TIMELINE_SEQUENCER.md](TIMELINE_SEQUENCER.md)** - Timeline-based scheduling
    - Timeline configuration
    - Trigger types
    - Calendar features
    - Web interface usage

## 🎨 Customization

14. **[StyleSystem.md](StyleSystem.md)** - Web UI styling system
    - Design tokens
    - Color system
    - Typography
    - Components
    - **Rebranding guide** (new!)
    - Customization examples

## 👨‍💻 Development

15. **[CONTRIBUTING.md](CONTRIBUTING.md)** - Contribution guidelines
    - Development setup
    - Coding standards
    - Testing guidelines
    - Pull request process

16. **[CHANGELOG.md](CHANGELOG.md)** - Version history
    - Release notes
    - Breaking changes
    - New features

## 📖 Documentation by Use Case

### "I want to get started quickly"
→ [QUICKSTART.md](QUICKSTART.md)

### "I need to wire up my hardware"
→ [HARDWARE.md](HARDWARE.md)

### "I want to configure my display"
→ [CONFIGURATION.md](CONFIGURATION.md)

### "I need to integrate with ESP32"
→ [INTEGRATION.md](INTEGRATION.md)

### "I want to use the API"
→ [docs/API_REFERENCE.md](docs/API_REFERENCE.md)

### "I want to create presets"
→ [preset_examples/](preset_examples/) and [schemas/preset-display.schema.json](schemas/preset-display.schema.json)

### "I want to schedule displays"
→ [TIMELINE_SEQUENCER.md](TIMELINE_SEQUENCER.md)

### "I want to customize the look"
→ [StyleSystem.md](StyleSystem.md)

### "I want to contribute code"
→ [CONTRIBUTING.md](CONTRIBUTING.md)

### "I need to sync multiple displays"
→ [docs/UDP_SYNCHRONIZATION.md](docs/UDP_SYNCHRONIZATION.md)

## 📁 Documentation Structure

```
lumigrid_display_node/
├── README.md                          # Main overview
├── QUICKSTART.md                      # Quick start guide
├── CONFIGURATION.md                   # Config reference
├── CONTRIBUTING.md                    # Development guide
├── INTEGRATION.md                     # ESP32 integration
├── HARDWARE.md                        # Hardware setup
├── StyleSystem.md                     # UI customization
├── TIMELINE_SEQUENCER.md              # Scheduling
├── EXAMPLES.md                        # API examples
├── CHANGELOG.md                       # Version history
├── DOCS_INDEX.md                      # This file
│
├── docs/
│   ├── API_REFERENCE.md               # Complete API docs
│   ├── UDP_SYNCHRONIZATION.md         # UDP sync protocol
│   └── PRESET_SCHEMA_VALIDATION.md    # Schema validation
│
├── preset_examples/
│   ├── README.md                      # Preset usage guide
│   ├── time_displays/                 # Clock presets
│   ├── temperature_displays/          # Temperature presets
│   └── message_displays/              # Message presets
│
└── schemas/
    └── preset-display.schema.json     # Preset JSON schema
```

## 🆕 Recent Additions

### New Documentation (Latest Update)

- ✨ **QUICKSTART.md** - Complete beginner's guide
- ✨ **INTEGRATION.md** - ESP32 integration with code examples
- ✨ **CONTRIBUTING.md** - Developer guidelines
- ✨ **CONFIGURATION.md** - Complete config reference
- ✨ **preset_examples/** - 11 ready-to-use preset files

### Enhanced Documentation

- 🔄 **README.md** - Improved navigation and structure
- 🔄 **API_REFERENCE.md** - Added 30+ missing endpoints
- 🔄 **StyleSystem.md** - Added comprehensive customization guide

## 🔍 Finding What You Need

### Search by Topic

**Installation & Setup**
- [QUICKSTART.md](QUICKSTART.md) - Quick installation
- [HARDWARE.md](HARDWARE.md) - Hardware setup
- [CONFIGURATION.md](CONFIGURATION.md) - Configuration

**API & Programming**
- [docs/API_REFERENCE.md](docs/API_REFERENCE.md) - API documentation
- [EXAMPLES.md](EXAMPLES.md) - Code examples
- [INTEGRATION.md](INTEGRATION.md) - ESP32 integration

**Customization**
- [CONFIGURATION.md](CONFIGURATION.md) - System config
- [preset_examples/](preset_examples/) - Display presets
- [StyleSystem.md](StyleSystem.md) - UI customization
- [TIMELINE_SEQUENCER.md](TIMELINE_SEQUENCER.md) - Scheduling

**Development**
- [CONTRIBUTING.md](CONTRIBUTING.md) - How to contribute
- [docs/API_REFERENCE.md](docs/API_REFERENCE.md) - API contracts
- [schemas/preset-display.schema.json](schemas/preset-display.schema.json) - Data schemas

## 🤝 Getting Help

- **GitHub Issues**: https://github.com/algorhythmicdev/lumigrid_display_node/issues
- **Discussions**: https://github.com/algorhythmicdev/lumigrid_display_node/discussions

## 📝 Contributing to Documentation

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on improving documentation.

Key principles:
- Keep it clear and concise
- Include code examples
- Cross-link related docs
- Update index when adding new docs

---

**Last Updated**: 2024-01-08

**Documentation Version**: 2.0 (Complete Rewrite)
