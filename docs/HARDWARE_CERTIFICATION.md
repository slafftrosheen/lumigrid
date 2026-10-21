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

Sources: `LumigridLedNode/components/certification/CertificationPlan.cpp`, `CertificationService.cpp`, `CertificationStore.cpp`, `include/CertificationTypes.hpp`, and `components/factory/FactoryApi.cpp`. See [validation checklist](HARDWARE_VALIDATION.md).
