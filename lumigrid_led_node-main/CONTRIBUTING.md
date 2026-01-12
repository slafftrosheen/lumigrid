# Contributing to LumiGrid

Thank you for your interest in contributing to LumiGrid! This document provides guidelines for contributing to the project.

## 📚 Documentation

For comprehensive developer documentation, see:
- **Interactive Docs:** Access the web interface at `http://<node-ip>/docs` and navigate to the Developer Guide
- **[DEVELOPMENT_GUIDE.md](Docs/DEVELOPMENT_GUIDE.md)** - Complete development documentation
- **[ARCHITECTURE.md](Docs/ARCHITECTURE.md)** - System architecture
- **[API_REFERENCE.md](Docs/API_REFERENCE.md)** - REST API reference

## Development Setup

### Prerequisites
- ESP-IDF v5.3 or later
- Git
- Basic knowledge of C and ESP32 development

### Setting Up Your Environment

1. **Install ESP-IDF**
   ```bash
   # Install system dependencies (Ubuntu/Debian)
   sudo apt-get update && sudo apt-get install -y \
     git wget flex bison gperf cmake ninja-build ccache libffi-dev libssl-dev \
     python3 python3-venv python3-pip python3-setuptools \
     dfu-util libusb-1.0-0

   # Fetch ESP-IDF v5.3 and tools
   git clone --depth=1 --branch v5.3 https://github.com/espressif/esp-idf.git ~/esp/esp-idf
   bash ~/esp/esp-idf/install.sh esp32
   . ~/esp/esp-idf/export.sh
   ```

2. **Clone the Repository**
   ```bash
   git clone https://github.com/algorhythmicdev/lumigrid.git
   cd lumigrid
   ```

3. **Build the Project**
   ```bash
   cd firmware
   idf.py set-target esp32
   idf.py build
   ```

## Code Quality Standards

### Compiler Warnings
All code must compile without warnings. The following warnings are treated as errors:
- `-Werror=format` - Format string safety
- `-Werror=int-conversion` - Type conversion issues
- `-Werror=sizeof-array-div` - Array size calculation errors

### Coding Style
- Use consistent indentation (2 spaces)
- No single-byte buffers (use `char buf[N]` where N > 1)
- Always check return values from functions that can fail
- Use cJSON getters with type checks:
  ```c
  // ❌ Bad - no type check
  int value = cJSON_GetObjectItem(obj, "key")->valueint;
  
  // ✅ Good - with type check
  const cJSON *item = cJSON_GetObjectItem(obj, "key");
  if (item && cJSON_IsNumber(item)) {
      int value = item->valueint;
  }
  ```

### Testing
- Write unit tests for new functionality
- Tests use Unity framework and are located in `firmware/main/test/`
- All tests must pass before merging

## Continuous Integration

GitHub Actions automatically runs on all pull requests:
1. **Build** - Compiles firmware with ESP-IDF v5.3
2. **Test** - Runs Unity unit tests
3. **Code Quality** - Checks for common issues

All checks must pass before a PR can be merged.

## Pull Request Process

1. **Fork the Repository**
   - Create your own fork of the project

2. **Create a Feature Branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **Make Your Changes**
   - Follow the code quality standards
   - Add tests for new functionality
   - Update documentation as needed

4. **Validate Your Changes**
   ```bash
   # Run pre-commit validation
   ./scripts/validate-code.sh
   ```

5. **Test Your Changes**
   ```bash
   cd firmware
   idf.py build
   idf.py build test
   ```

6. **Commit Your Changes**
   ```bash
   git add .
   git commit -m "Brief description of changes"
   ```

7. **Push to Your Fork**
   ```bash
   git push origin feature/your-feature-name
   ```

8. **Open a Pull Request**
   - Provide a clear description of the changes
   - Reference any related issues
   - Wait for CI checks to pass
   - Address review feedback

## Adding New Components

### LED Effects
1. Create effect functions in `components/led_effects/effects_pack_*.c`
2. Register the effect in `components/led_effects/effects.c`
3. Add tests in `firmware/main/test/`

### REST API Endpoints
1. Add handler in `components/rest_api/rest_api.c`
2. Update API documentation in `Docs/api_spec.yaml`
3. Test the endpoint manually and add automated tests

### Hardware Drivers
1. Create component directory under `firmware/components/`
2. Add CMakeLists.txt with proper warning flags
3. Include comprehensive error handling
4. Document the driver interface in header files

## Component CMakeLists.txt Template

When creating a new component, use this template:

```cmake
idf_component_register(
    SRCS "your_component.c"
    INCLUDE_DIRS "include"
    REQUIRES driver  # Add required components
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wall 
    -Wextra
    -Werror=format
    -Werror=int-conversion
    -Werror=sizeof-array-div
)
```

## Reporting Issues

When reporting bugs, please include:
- ESP32 board/module type
- ESP-IDF version
- Steps to reproduce
- Expected vs actual behavior
- Relevant log output

## Questions?

- Check existing [Issues](https://github.com/algorhythmicdev/lumigrid_led_node/issues)
- Review the [Documentation](Docs/) or web interface at `http://<node-ip>/docs`
- See [TROUBLESHOOTING.md](Docs/TROUBLESHOOTING.md) for common issues
- Open a new issue for discussion

## License

By contributing to LumiGrid, you agree that your contributions will be licensed under the project's license (TBD).
