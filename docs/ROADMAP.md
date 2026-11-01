# Open work

This is a source-observed gap list, not release dates or delivery promises.

| Work | Completion evidence |
| --- | --- |
| Production signing/provisioning | The advertised signed-OTA flag now follows the effective build setting; the default candidate disables enforcement. Agree, implement and validate the production signing/key-provisioning policy before enabling it. |
| Release delivery | Versioned artifacts/hashes, matching layout and verified manifest if offered |
| MQTT security | Intended broker credential/certificate configuration and validation |
| Hardware acceptance | Per-channel and combined RMT/PWM/Wi-Fi measurements on exact build |
| Multi-node timing | Measured sync and loss/reconnect with matching content |
| Power commissioning | Actual loads/supplies compared with modeled values |

Implemented functions are described in the current guides. See [source audit](SOURCE_OF_TRUTH.md) and [hardware validation](HARDWARE_VALIDATION.md). Earlier plans in history are not current commitments.
