# Hardware acceptance workflow

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

Only the currently executing manual test in `waiting_for_operator` accepts external evidence. Starting an attempt clears its previous measurement and note. A result cannot create evidence for an unstarted test, replace an automatic probe, overwrite a completed attempt or bypass the physical runner's matching owner/session. Attempts count starts, including blocked executions; the counter never wraps. After 255 attempts, start a new session.

Serial numbers are limited to 31 UTF-8 bytes, units to 7, and evidence notes to 95. Oversized strings are rejected, not truncated. The bench combines the entered measurement method and observation into the persisted note and shows the shared byte budget. A zero reading is valid and is retained in both failed and passed measurements. Both outcomes need the plan's exact unit. Failure needs an explanation; a requested pass outside the plan tolerance is recorded as failed.

### Binding requests to the observed session and attempt

Current status advertises `evidenceBinding: "session_attempt_v1"`. The current bench requires that capability for writes and remains read-only on older firmware. It captures the selected node, authentication session and evidence identity before each action. Status must be current; a later node selection never retargets a pending command.

| Action | Binding fields |
| --- | --- |
| Start a new session | `certificationId` from the previous status; an empty string is valid before the first session |
| Start a test or abort | Current `certificationId` |
| Submit a result | Current `certificationId`, `testStartedMs` from the result's `startedMs`, and `testAttempt` from its `attempts` |

Session IDs include a random 64-bit suffix so uptime reuse after reboot does not reproduce the former ID. A mismatched session or attempt returns HTTP 409 before the requested mutation. All supplied binding fields must be valid; partial result bindings are rejected. Legacy service tools that omit every binding field still use the active-test guard, but do not have the new protection against a delayed request targeting a later attempt. Migrate those tools to the complete binding tuple.

### Physical confirmation and cancellation

A successful physical-result receipt means evidence was accepted for processing. Status reports `confirmationPending: true`, retains `currentTest`, and keeps the durable test `running` until the matching runner finishes blackout. New tests and sessions remain blocked. The worker disables ongoing test/stream/loop producers before stopping output, and publishes terminal acknowledgment afterward. A failed blackout records a failed result and error code, preserving the operator's observation and measurement method. Stale or duplicate worker confirmations cannot stop another session.

The service only finalizes evidence after that matching acknowledgment and inactive output/test flags. Pending evidence is not persisted as passed, so reboot cannot turn an unacknowledged stop into a completed pass. Configuration or image invalidation supersedes pending confirmation and requests scoped cleanup; a late success cannot restore invalidated evidence.

An abort response of HTTP 202 with `cancellation_requested` means cancellation was accepted. Keep observing `cancellationPending` and the owned test before reporting completion. The acceptance page uses the session-bound abort; it does not follow it with an unrelated generic factory abort. Software output acknowledgment is still not an electrical measurement.

Persistence errors stop acceptance and leave the session incomplete with `persistence_failed`; export the in-memory report before rebooting. The existing 16 KB NVS partition is shared with configuration and credentials, so capacity must be verified on the intended production configuration. A successful API response alone does not prove recovery after power loss.

Exported `reportingFirmwareVersion` and `reportingFirmwareElfSha256` identify the image generating the report. They do not retrospectively establish the image that collected an older session. Keep the candidate manifest and original board observations alongside each report.

`startedMs` and `completedMs` are device uptime values, not Unix timestamps. Use them for elapsed durations within a session and retain an independently recorded wall-clock time with external bench evidence. The dashboard must not turn them into calendar dates.

See [firmware candidate and bench gates](FIRMWARE_RELEASE_CANDIDATE.md) for the current software verification scope and hardware acceptance sequence.

Sources: `LumigridLedNode/components/certification/CertificationPlan.cpp`, `CertificationService.cpp`, `CertificationStore.cpp`, `include/CertificationTypes.hpp`, and `components/factory/FactoryApi.cpp`. See [validation checklist](HARDWARE_VALIDATION.md).
