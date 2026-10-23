# Certification workflow

LumiGrid's certification service is an internal engineering acceptance workflow, not third-party regulatory certification.

`CertificationPlan.cpp` defines tests, categories, timeouts, evidence sources and per-profile requirement levels. Some tests require observations or external measurements. Read `/api/v1/certification/plan` rather than reusing an old list.

1. Identify actual device/build/topology.
2. Read plan/status, choose the intended profile and inspect required/optional tests.
3. Start the session/test through the UI or API.
4. Perform the requested observation/measurement before submitting results.
5. Inspect failures and abort if target/wiring is wrong.
6. Export `/api/v1/certification/report` with revision and measurement context.

Registered endpoints cover plan, status, start, test/start, test/result, abort and report. Factory tests/burn-in are separate; success there does not satisfy all certification requirements.

Output commands do not prove visible patterns. Power estimates are not current measurements. Logical E-stop is not a physical input: current board flags declare no physical E-stop input or addressable power gate.

## Candidate record and evidence rules

The `1.0.0-rc1` candidate uses plan version 2 and record schema 2: all 46 plan entries fit and their complete IDs are retained. Older records are not accepted as current evidence. Export existing evidence before updating; old reports remain scoped to the image, board and plan that produced them.

Every required test must pass before the session can pass. Pending, running, waiting, skipped, blocked or missing required results leave it incomplete. Instrument results require finite numeric values and matching units; explicitly failed results remain failures even when the number falls within tolerance. Public result submission cannot claim the internal automatic evidence source. Tests without implemented physical/reboot evidence remain blocked.

Persistence errors stop acceptance and leave the session incomplete with `persistence_failed`; export the in-memory report before rebooting. The existing 16 KB NVS partition is shared with configuration and credentials, so capacity must be verified on the intended production configuration. A successful API response alone does not prove recovery after power loss.

Exported `reportingFirmwareVersion` and `reportingFirmwareElfSha256` identify the image generating the report. They do not retrospectively establish the image that collected an older session. Keep the candidate manifest and original board observations alongside each report.

See [firmware candidate and bench gates](FIRMWARE_RELEASE_CANDIDATE.md) for the current software verification scope and hardware acceptance sequence.

Sources: `LumigridLedNode/components/certification/CertificationPlan.cpp`, `CertificationService.cpp`, `CertificationStore.cpp`, `include/CertificationTypes.hpp`, and `components/factory/FactoryApi.cpp`. See [validation checklist](HARDWARE_VALIDATION.md).
