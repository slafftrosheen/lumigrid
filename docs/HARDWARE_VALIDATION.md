# Hardware validation

Source inspection proves implementation, host tests prove their model, and API success is a software observation. Electrical/installation acceptance requires actual measurements.

Record Git SHA plus dirty changes, build environment, artifact hash, board revision, flash size, supplies, loads/counts, wiring, network and instruments. Keep credentials/private backups out of reports.

| Area | Required evidence |
| --- | --- |
| First boot | Generated AP access, Admin creation, outputs off before configuration |
| Reset/recovery | Measured /OE/data behavior during power-up and reboot |
| Addressable | A1–A8 mapping, order, intended count, crosstalk |
| PWM | P1–P8 stepped duty and /OE inhibit |
| Combined | Intended RMT/PWM topology with Wi-Fi/protocol activity |
| Power | Actual supply/load alongside complete/partial/unknown estimates |
| Playback | Transitions, takeover/release, DMX timeout, cues/timelines |
| Multi-node | Matching manifests, measured timing, loss/reconnect |
| Storage/OTA | Backup/restore, rejection, boot verification/rollback |
| Endurance | Sustained load, memory/stack/reset/transport counters |

Record PASS, FAIL or NOT_RUN with evidence. Missing observations are not passes. Historical logs retain only their original scope.

Tools include factory/certification dashboards, corresponding API families, diagnostics and `scripts/hardware_coexistence_soak.py`. Review target/arguments before running; these tools can change outputs.

No new flashing or device measurements are claimed by this documentation change. See [certification](HARDWARE_CERTIFICATION.md).
