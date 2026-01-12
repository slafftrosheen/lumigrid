# LumiGrid LED Node - Development Guide

**Version:** 1.0.0  
**Target Platform:** ESP32-WROOM-32U  
**Framework:** ESP-IDF v5.3+  
**Last Updated:** 2026-01-08

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Project Structure](#project-structure)
3. [Coding Standards](#coding-standards)
4. [Adding New Features](#adding-new-features)
5. [Testing](#testing)
6. [Code Review Process](#code-review-process)
7. [Security Considerations](#security-considerations)
8. [Contributing](#contributing)

---

## Getting Started

### Prerequisites

Before contributing, ensure you have:
- **ESP-IDF v5.3** installed and working
- Familiarity with **C programming** and embedded systems
- Understanding of **FreeRTOS** concepts
- **Git** for version control
- **ESP32 development board** for testing

**See:** [SETUP_GUIDE.md](SETUP_GUIDE.md) for detailed setup instructions.

### Development Environment Setup

1. **Clone the repository:**
   ```bash
   git clone https://github.com/algorhythmicdev/lumigrid_led_node.git
   cd lumigrid_led_node
   ```

2. **Activate ESP-IDF:**
   ```bash
   . ~/esp/esp-idf/export.sh
   ```

3. **Build the project:**
   ```bash
   cd firmware
   idf.py build
   ```

4. **Run pre-commit validation:**
   ```bash
   ./scripts/validate-code.sh
   ```

---

## Project Structure

### Directory Layout

```
lumigrid_led_node/
├── firmware/                      # Main firmware project
│   ├── main/                      # Application code
│   │   ├── app_main.c             # Entry point
│   │   ├── config/                # Configuration files
│   │   ├── tasks/                 # FreeRTOS tasks
│   │   ├── utils/                 # Utility modules
│   │   └── include/               # Public headers
│   ├── components/                # Reusable components
│   │   ├── led_effects/           # Effect rendering engine
│   │   ├── pca9685_driver/        # PWM controller driver
│   │   ├── rest_api/              # HTTP REST endpoints
│   │   ├── sync_protocol/         # UDP multicast sync
│   │   ├── mqtt_wrapper/          # MQTT client
│   │   └── ...
│   ├── assets/                    # Web UI and resources
│   ├── schemas/                   # JSON validation schemas
│   └── test/                      # Unit and integration tests
├── Docs/                          # Documentation
├── scripts/                       # Build and utility scripts
└── README.md                      # Project overview
```

### Key Files

- `firmware/CMakeLists.txt` - Root CMake configuration
- `firmware/sdkconfig.defaults` - ESP-IDF default settings
- `firmware/main/config/default_config.json` - Default node configuration
- `firmware/schemas/preset-led.schema.json` - Preset validation schema
- `Docs/ARCHITECTURE.md` - System architecture
- `Docs/api_spec.yaml` - OpenAPI specification

---

## Coding Standards

### C Code Style

**General Rules:**
- **Indentation:** 2 spaces (no tabs)
- **Line length:** Maximum 120 characters
- **Naming:**
  - Functions: `snake_case` (e.g., `fx_render_solid`)
  - Variables: `snake_case` (e.g., `pixel_count`)
  - Constants: `UPPER_SNAKE_CASE` (e.g., `MAX_PIXELS`)
  - Types: `snake_case_t` (e.g., `effect_params_t`)
- **Bracing:** K&R style (opening brace on same line)

**Example:**
```c
esp_err_t fx_render_solid(aled_channel_t *ch, const effect_params_t *p,
                          uint32_t t_ms, uint32_t t_end_ms) {
  if (!ch || !p) {
    return ESP_ERR_INVALID_ARG;
  }
  
  for (int i = 0; i < ch->n_pixels; i++) {
    ch->framebuf[i] = p->color1;
  }
  
  return ESP_OK;
}
```

### Header Files

**Structure:**
```c
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Type definitions
typedef struct {
  uint8_t r, g, b, w;
} px_rgba_t;

// Function declarations
esp_err_t fx_init(void);
esp_err_t fx_render(uint32_t t_ms);

#ifdef __cplusplus
}
#endif
```

### Error Handling

**Always check return values:**
```c
// Good
esp_err_t ret = i2c_master_write_byte(cmd, data, true);
if (ret != ESP_OK) {
  ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
  return ret;
}

// Bad
i2c_master_write_byte(cmd, data, true);  // Ignoring errors!
```

### Memory Management

**Rules:**
- Check all allocations for NULL
- Free memory in reverse order of allocation
- Use `ESP_LOGI/E/W/D` for logging (not `printf`)
- Avoid allocations in hot paths (e.g., effect render loop)

**Example:**
```c
char *buf = malloc(256);
if (!buf) {
  ESP_LOGE(TAG, "Failed to allocate buffer");
  return ESP_ERR_NO_MEM;
}

// Use buffer...

free(buf);
```

### Compiler Warnings

**Treat these as errors:**
- `-Werror=format` - Format string vulnerabilities
- `-Werror=int-conversion` - Type conversion issues
- `-Werror=sizeof-array-div` - Array size calculation errors

**Configuration:** Already set in `firmware/CMakeLists.txt`

### Buffer Safety

**Never use single-byte buffers:**
```c
// Bad
char buf[1];

// Good
char buf[64];  // Reasonable size
```

### JSON Handling

**Always use type checks:**
```c
// Good
cJSON *obj = cJSON_GetObjectItem(root, "speed");
if (cJSON_IsNumber(obj)) {
  float speed = (float)cJSON_GetNumberValue(obj);
}

// Bad
float speed = cJSON_GetObjectItem(root, "speed")->valuedouble;  // May crash!
```

---

## Adding New Features

### Adding a New LED Effect

1. **Create effect function in `components/led_effects/effects.c`:**
   ```c
   static void fx_myeffect_render(aled_channel_t *ch, const effect_params_t *p,
                                   uint32_t t_ms, uint32_t t_end_ms) {
     // Your effect code here
     for (int i = 0; i < ch->n_pixels; i++) {
       ch->framebuf[i].r = ...;
       ch->framebuf[i].g = ...;
       ch->framebuf[i].b = ...;
     }
   }
   ```

2. **Register in effect table:**
   ```c
   static const effect_vtable_t EFFECTS[] = {
     {1, "solid", NULL, fx_solid_render},
     {2, "gradient", NULL, fx_gradient_render},
     // ... existing effects
     {10, "myeffect", NULL, fx_myeffect_render},
   };
   ```

3. **Update effect ID range in schema** (`schemas/preset-led.schema.json`)

4. **Test the effect:**
   ```bash
   curl -X POST http://192.168.4.1/api/presets \
     -d '{"preset_name":"test","effect_id":10,"speed":1.0}'
   ```

### Adding a New REST Endpoint

1. **Add handler in `components/rest_api/rest_api.c`:**
   ```c
   static esp_err_t my_endpoint_handler(httpd_req_t *req) {
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "message", "Hello World");
     
     char *json_str = cJSON_PrintUnformatted(root);
     httpd_resp_set_type(req, "application/json");
     httpd_resp_sendstr(req, json_str);
     
     free(json_str);
     cJSON_Delete(root);
     return ESP_OK;
   }
   ```

2. **Register handler:**
   ```c
   httpd_uri_t my_endpoint = {
     .uri = "/api/myendpoint",
     .method = HTTP_GET,
     .handler = my_endpoint_handler,
     .user_ctx = NULL
   };
   
   httpd_register_uri_handler(server, &my_endpoint);
   ```

3. **Update API documentation** (`Docs/api_spec.yaml` and `API_REFERENCE.md`)

4. **Test:**
   ```bash
   curl http://192.168.4.1/api/myendpoint
   ```

### Adding a New Component

1. **Create component directory:**
   ```bash
   mkdir firmware/components/my_component
   cd firmware/components/my_component
   ```

2. **Create CMakeLists.txt:**
   ```cmake
   idf_component_register(
     SRCS "my_component.c"
     INCLUDE_DIRS "include"
     REQUIRES "driver" "esp_log"
   )
   ```

3. **Create header:** `include/my_component.h`

4. **Create source:** `my_component.c`

5. **Add to main component requirements** in `firmware/main/CMakeLists.txt`:
   ```cmake
   idf_component_register(
     ...
     REQUIRES ... my_component
   )
   ```

---

## Testing

### Pre-Commit Validation

**Always run before committing:**
```bash
./scripts/validate-code.sh
```

**Checks for:**
- Single-byte buffers
- Format string vulnerabilities
- Unsafe cJSON usage
- Memory allocation patterns
- TODO/FIXME markers

### Unit Tests

**Location:** `firmware/components/*/test/`

**Structure:**
```c
#include "unity.h"
#include "my_component.h"

TEST_CASE("test basic functionality", "[my_component]") {
  esp_err_t ret = my_function();
  TEST_ASSERT_EQUAL(ESP_OK, ret);
}
```

**Run tests:**
```bash
cd firmware
idf.py build test
```

### Integration Tests

**Target tests** using pytest-embedded:

**Location:** `firmware/main/test/`

**Example:**
```python
def test_effect_engine(dut):
    dut.expect("Effect engine initialized")
    dut.write("fx_test")
    dut.expect("Test passed")
```

**Run:**
```bash
cd firmware
pytest --target esp32
```

### Manual Testing

**Recommended test sequence:**
1. **Build and flash:**
   ```bash
   idf.py build flash monitor
   ```

2. **Verify boot:**
   - Check serial output for errors
   - Verify Wi-Fi AP starts
   - Connect to web UI

3. **Test REST API:**
   ```bash
   curl http://192.168.4.1/api/status
   curl http://192.168.4.1/api/fx
   ```

4. **Test effects:**
   - Create test preset via API
   - Verify LEDs respond
   - Check FPS in status

5. **Test sync protocol** (if applicable):
   - Configure master/slave
   - Verify packets with Wireshark
   - Check drift over time

6. **Stress test:**
   - Run all channels at max
   - Monitor heap usage
   - Check for memory leaks

### Performance Testing

**Metrics to measure:**
- Effect loop time (target: ≤3ms for 8×180px @ 60fps)
- Free heap (target: ≥80KB during playback)
- REST response time (target: <50ms)
- Sync drift (target: <5ms over 60s)

**Tools:**
```c
// Measure execution time
uint64_t t0 = esp_timer_get_time();
fx_render_all();
uint64_t t1 = esp_timer_get_time();
ESP_LOGI(TAG, "Render took %llu us", t1 - t0);

// Check heap
size_t free_heap = esp_get_free_heap_size();
ESP_LOGI(TAG, "Free heap: %u bytes", free_heap);
```

---

## Code Review Process

### Before Submitting PR

1. **Run validation:**
   ```bash
   ./scripts/validate-code.sh
   ```

2. **Build without warnings:**
   ```bash
   idf.py build
   ```

3. **Test on hardware** (if possible)

4. **Update documentation** if API changed

5. **Check commit messages** are descriptive

### PR Template

```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Documentation update
- [ ] Performance improvement

## Testing
- [ ] Builds without warnings
- [ ] Validation script passes
- [ ] Tested on hardware
- [ ] Unit tests added/updated

## Documentation
- [ ] Updated API_REFERENCE.md
- [ ] Updated relevant guides
- [ ] Code comments added
```

### Review Checklist

**Reviewers check for:**
- Code follows style guidelines
- No compiler warnings
- Error handling present
- Memory leaks prevented
- Security vulnerabilities addressed
- Documentation updated
- Tests included

---

## Security Considerations

### Input Validation

**Always validate external inputs:**
```c
// Validate preset payload
if (!cJSON_IsNumber(effect_id_obj)) {
  httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "effect_id must be integer");
  return ESP_FAIL;
}

int effect_id = cJSON_GetNumberValue(effect_id_obj);
if (effect_id < 1000 || effect_id > 9999) {
  httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "effect_id out of range");
  return ESP_FAIL;
}
```

### Buffer Overflow Prevention

**Use safe string functions:**
```c
// Good
char buf[64];
snprintf(buf, sizeof(buf), "Value: %d", value);

// Bad
char buf[64];
sprintf(buf, "Value: %d", value);  // No bounds checking!
```

### Format String Vulnerabilities

**Never use user input as format string:**
```c
// Bad (DANGEROUS!)
ESP_LOGI(TAG, user_input);

// Good
ESP_LOGI(TAG, "%s", user_input);
```

### Security Scan

**Run CodeQL before finalizing:**
```bash
# Will be run automatically by CI
# Check for common vulnerabilities
```

### Secrets Management

**Never hardcode credentials:**
```c
// Bad
const char *wifi_password = "MyPassword123";

// Good
wifi_config_t cfg;
nvs_get_str(nvs_handle, "wifi_pass", cfg.password, sizeof(cfg.password));
```

---

## Contributing

### Contribution Workflow

1. **Fork the repository**

2. **Create feature branch:**
   ```bash
   git checkout -b feature/my-new-feature
   ```

3. **Make changes and commit:**
   ```bash
   git add .
   git commit -m "Add new feature: description"
   ```

4. **Push to fork:**
   ```bash
   git push origin feature/my-new-feature
   ```

5. **Create Pull Request** on GitHub

### Commit Message Guidelines

**Format:**
```
type: Short summary (50 chars max)

Longer description if needed. Wrap at 72 characters.

- Bullet points for multiple changes
- Reference issues: Fixes #123
```

**Types:**
- `feat:` New feature
- `fix:` Bug fix
- `docs:` Documentation only
- `style:` Code style changes (formatting)
- `refactor:` Code refactoring
- `perf:` Performance improvement
- `test:` Adding/updating tests
- `chore:` Build process, tooling

**Examples:**
```
feat: Add ocean wave LED effect

Implements a realistic ocean wave effect with configurable
speed and color depth. Uses perlin noise for natural motion.

- Added fx_ocean_render() function
- Updated effect registry
- Added preset examples
```

```
fix: Prevent buffer overflow in preset validation

Adds bounds checking to prevent overflow when parsing
preset names longer than 64 characters.

Fixes #234
```

### License

By contributing, you agree that your contributions will be licensed under the project's license (TBD).

---

## Additional Resources

- **ESP-IDF Programming Guide:** https://docs.espressif.com/projects/esp-idf/en/latest/
- **FreeRTOS Documentation:** https://www.freertos.org/Documentation/
- **C Coding Standards:** MISRA C or JPL standards
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md)
- **API Specification:** [api_spec.yaml](api_spec.yaml)

---

## Development Tips

### Debugging

**Serial debugging:**
```c
ESP_LOGD(TAG, "Variable x = %d", x);  // Debug level
ESP_LOGI(TAG, "Initialized");         // Info level
ESP_LOGW(TAG, "Warning condition");   // Warning
ESP_LOGE(TAG, "Error: %s", error);    // Error
```

**Set log levels in menuconfig:**
```bash
idf.py menuconfig
# Component config → Log output → Default log verbosity → Debug
```

**GDB debugging:**
```bash
# With JTAG adapter
idf.py openocd gdb
```

### Optimization

**Profile code:**
```c
#include "esp_timer.h"

uint64_t t0 = esp_timer_get_time();
// Code to profile
uint64_t elapsed = esp_timer_get_time() - t0;
ESP_LOGI(TAG, "Took %llu us", elapsed);
```

**Optimize hot paths:**
- Move constants outside loops
- Use lookup tables for sin/cos
- Minimize memory allocations
- Use DMA where possible

### Common Pitfalls

1. **Not checking malloc() returns**
2. **Using sprintf() instead of snprintf()**
3. **Ignoring function return values**
4. **Stack overflow (too small stack size)**
5. **Watchdog timeouts (blocking too long)**
6. **Race conditions (missing mutexes)**

---

## Support

- **Issues:** https://github.com/algorhythmicdev/lumigrid_led_node/issues
- **Discussions:** GitHub Discussions
- **Pull Requests:** Welcome!

---

**Last Updated:** 2026-01-08  
**Compatible Firmware:** v1.0.0+  
**Maintained by:** LumiGrid Development Team
