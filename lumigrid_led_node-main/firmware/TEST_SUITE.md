# Unit Test Suite Documentation

## Overview
This document describes the comprehensive unit test suite created for the LumiGrid firmware. The tests use the Unity testing framework, which is native to ESP-IDF.

## Test Structure

### Test Location
All tests are located in: `firmware/main/test/`

### Test Files

#### 1. test_json_config.c
Tests for JSON configuration parsing and serialization.

**Test Cases:**
- `config parses valid json` - Validates parsing of well-formed JSON
- `config serializes to valid json string` - Tests JSON serialization
- `config handles invalid json gracefully` - Validates error handling
- `config merges nested objects correctly` - Tests merge functionality
- `config validates field types` - Type checking validation
- `config handles arrays correctly` - Array handling
- `config safe access prevents crashes` - Safe access patterns

**Coverage:**
- JSON parsing with cJSON library
- Type validation (string, number, bool, null)
- Nested object handling
- Array operations
- Error handling for malformed JSON

#### 2. test_effect_params.c
Tests for effect parameter validation and handling.

**Test Cases:**
- `effect params have valid defaults` - Default parameter validation
- `effect speed parameter validates range` - Speed range (0.0-5.0)
- `effect intensity parameter validates range` - Intensity range (0.0-1.0)
- `effect brightness parameter validates range` - Brightness range (0.0-1.0)
- `effect color parameter handles rgb values` - RGB color handling
- `effect params structure is correctly sized` - Structure validation
- `effect palette_id handles valid range` - Palette ID (0-255)
- `effect params can be safely copied` - Parameter copying
- `effect params handle floating point edge cases` - Edge case handling
- `u8 lerp interpolates correctly` - Linear interpolation test

**Coverage:**
- Parameter range validation
- Clamping logic
- RGB color extraction and composition
- Structure size and alignment
- Floating-point edge cases

#### 3. test_sync_protocol.c
Tests for synchronization protocol packet encoding/decoding.

**Test Cases:**
- `sync packet has correct size and alignment` - Structure validation
- `sync tick packet encodes correctly` - TICK packet encoding
- `sync cue packet encodes correctly` - CUE packet encoding
- `sync packet decodes from buffer` - Packet decoding
- `sync tempo set and get` - Tempo handling
- `sync phase wraps correctly` - Phase wrapping (0.0-1.0)
- `sync tempo converts to period correctly` - BPM to period conversion
- `sync phase increment calculated correctly` - Phase calculation
- `sync drift correction uses ewma` - EWMA drift correction
- `sync packet type validates correctly` - Packet type validation
- `sync handles timestamp overflow` - Overflow handling
- `sync packet marshals to bytes correctly` - Byte marshalling
- `sync packet unmarshals from bytes correctly` - Byte unmarshalling
- `sync packet zero initialization` - Zero initialization

**Coverage:**
- Packet structure (8 bytes, packed)
- Encoding/decoding
- Tempo and phase calculations
- Drift correction algorithm
- Timestamp overflow handling
- Byte-level marshalling

#### 4. test_rest_api.c (Extended)
Tests for REST API JSON request/response formatting.

**Existing Tests:**
- `status json exposes telemetry` - Status endpoint validation
- `status event formatting wraps json` - SSE event formatting
- `preset delete path parsing` - Path parsing

**New Tests Added:**
- `rest api validates json request structure` - Request validation
- `rest api rejects malformed json` - Error handling
- `rest api parses hex color correctly` - Hex color parsing (#RRGGBB)
- `rest api rejects invalid hex colors` - Invalid color handling
- `rest api parses effect params from json` - Parameter parsing
- `rest api formats response json correctly` - Response formatting
- `rest api formats error responses` - Error response formatting
- `rest api formats array responses` - Array response formatting

**Coverage:**
- JSON request parsing
- Response formatting
- Error handling
- Hex color validation
- Array serialization

## Running Tests

### Prerequisites
1. ESP-IDF v5.3+ installed and configured
2. ESP32 toolchain set up
3. Unity test framework (included with ESP-IDF)

### Build and Run Tests

```bash
# Navigate to firmware directory
cd firmware

# Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Set target to ESP32
idf.py set-target esp32

# Build the project (includes tests)
idf.py build

# Run tests
idf.py test
```

### Running Specific Test Cases

To run specific test cases, you can use Unity's filtering:

```bash
# Run only JSON config tests
idf.py test --filter "[json_config]"

# Run only effect parameter tests
idf.py test --filter "[effects]"

# Run only sync protocol tests
idf.py test --filter "[sync_protocol]"

# Run only REST API tests
idf.py test --filter "[rest]"
```

## Test Categories

Tests are organized using Unity tags:
- `[json_config]` - JSON configuration tests
- `[effects]` - Effect parameter tests
- `[sync_protocol]` - Sync protocol tests
- `[rest]` - REST API tests
- `[cue]` - Cue handling tests (existing)

## Adding New Tests

### Test File Structure

```c
#include "unity.h"
#include "your_component.h"

TEST_CASE("descriptive test name", "[tag]") {
    // Arrange
    // ... setup test data
    
    // Act
    // ... call function under test
    
    // Assert
    TEST_ASSERT_TRUE(condition);
    TEST_ASSERT_EQUAL_INT(expected, actual);
    TEST_ASSERT_NOT_NULL(pointer);
    // ... other assertions
}
```

### Common Unity Assertions

```c
// Boolean assertions
TEST_ASSERT_TRUE(condition);
TEST_ASSERT_FALSE(condition);

// Equality assertions
TEST_ASSERT_EQUAL_INT(expected, actual);
TEST_ASSERT_EQUAL_UINT32(expected, actual);
TEST_ASSERT_EQUAL_FLOAT(expected, actual);
TEST_ASSERT_EQUAL_STRING(expected, actual);

// Null/Not Null
TEST_ASSERT_NULL(pointer);
TEST_ASSERT_NOT_NULL(pointer);

// Range assertions
TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual);
TEST_ASSERT_INT_WITHIN(delta, expected, actual);
```

### Adding Tests to Build

1. Create test file in `firmware/main/test/`
2. Add to `CMakeLists.txt`:

```cmake
idf_component_register(SRCS "test_your_component.c"
                            # ... other test files
                    INCLUDE_DIRS "."
                    REQUIRES unity your_component # ... other dependencies
                    )
```

## Test Coverage Summary

| Component | Test File | Test Cases | Coverage |
|-----------|-----------|------------|----------|
| JSON Config | test_json_config.c | 8 | Parsing, serialization, validation |
| Effect Parameters | test_effect_params.c | 10 | Range validation, clamping, RGB |
| Sync Protocol | test_sync_protocol.c | 15 | Packet encode/decode, timing |
| REST API | test_rest_api.c | 10 | Request/response formatting |

**Total: 43+ test cases covering critical functionality**

## Best Practices

1. **Test one thing per test case** - Each test should validate a single behavior
2. **Use descriptive names** - Test names should clearly describe what is being tested
3. **Follow AAA pattern** - Arrange, Act, Assert
4. **Test edge cases** - Include boundary conditions and error cases
5. **Keep tests independent** - Tests should not depend on each other
6. **Clean up resources** - Free allocated memory, close files, etc.
7. **Use appropriate assertions** - Choose the most specific assertion for clarity

## Host-Based Testing

Many of these tests can run on the host (Linux/macOS) without ESP32 hardware:
- JSON config tests (uses standard cJSON)
- Effect parameter validation tests (pure logic)
- Sync protocol packet structure tests (byte manipulation)

For hardware-specific features (WiFi, GPIO, etc.), tests require ESP32 target.

## Continuous Integration

These tests can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions workflow
- name: Run Unit Tests
  run: |
    cd firmware
    . $IDF_PATH/export.sh
    idf.py set-target esp32
    idf.py build
    idf.py test
```

## Troubleshooting

### Build Errors

**Missing dependencies:**
```bash
# Ensure all required components are listed in CMakeLists.txt REQUIRES
```

**Include path errors:**
```bash
# Check that component headers are in public include directories
```

### Test Failures

1. Check test output for assertion details
2. Verify test assumptions match implementation
3. Check for resource leaks (memory, file handles)
4. Ensure tests are independent (no shared state)

## Future Enhancements

Potential areas for additional test coverage:
- PWM effects parameter validation
- Preset loading and validation
- Network protocol edge cases
- File system operations
- MQTT message handling
- Layout configuration validation

## References

- [Unity Test Framework Documentation](https://github.com/ThrowTheSwitch/Unity)
- [ESP-IDF Unit Testing](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)
- [ESP-IDF Build System](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html)
