# LED node hardware contract

The firmware targets ESP32 `esp32dev` with 8 MB flash. Its canonical topology is 16 channels: A1–A8 addressable and P1–P8 independent PWM dimmers. The PCA9685 has more chip outputs than the eight exposed by LumiGrid.

| Output | GPIO / interface |
| --- | --- |
| A1 | GPIO16 |
| A2 | GPIO4 |
| A3 | GPIO17 |
| A4 | GPIO18 |
| A5 | GPIO19 |
| A6 | GPIO23 |
| A7 | GPIO26 |
| A8 | GPIO27 |
| PCA9685 SDA / SCL | GPIO21 / GPIO22 |
| PCA9685 address | `0x55` (A0, A2, A4 high) |
| Active-low /OE | GPIO25 |

WS2812 accepts RGB/GRB; SK6812 accepts RGBW/GRBW. `MAX_FIXTURE_PIXELS` is 256. Topology admission also checks memory: a schema maximum does not promise every combination or fixed frame rate. Addressable output uses RMT; legacy SPI enum entries are not a supported new hardware family.

PWM outputs are independent dimmers. Compound RGB/RGBW fixture values remain for migration, not new configurations. Indices 0–7 mean A1–A8; 8–15 mean P1–P8. Group masks use those positions.

## Electrical boundaries

Early board safety holds /OE high and addressable data low. PWM enable depends on initialization and safe output state. Board flags report **no physical E-stop input** and **no addressable power gate**. Software blackout does not disconnect strip power.

Supply ratings, voltage, wiring, fusing, level conversion and environmental suitability must be established for the actual assembly. Power telemetry is modeled; see [configuration](CONFIGURATION.md).

Sources: `LumigridLedNode/components/board/include/lg_board_pins.hpp`, `board/BoardOutputSafety.cpp`, `config/include/OutputCapabilities.hpp`, `config/include/SignageTypes.hpp`, and `output/`.
