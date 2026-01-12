# LumiGrid LED Node

Firmware for ESP32-based addressable LED controllers, designed for the LumiGrid ecosystem.

## Getting Started

This project is part of the LumiGrid monorepo. All development tasks, including installation and testing, should be managed using the `Makefile` in the root of the repository.

### Prerequisites

- ESP-IDF (v5.3+)

### Installation

To install dependencies for all LumiGrid subprojects, run the following command from the repository root:

```bash
make install
```

### Building the Firmware

To build the firmware, navigate to this directory and use the ESP-IDF build system:

```bash
idf.py build
```

### Flashing the Firmware

```bash
idf.py -p /dev/ttyUSB0 flash
```

### Running Tests

To run the unit tests for this project, use the ESP-IDF test command:

```bash
idf.py test
```
