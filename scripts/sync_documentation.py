"""Bundle shared guides and extract registered firmware routes/capacities.

Run from any directory. --check is read-only and fails on stale output or a
broken current-guide Markdown link. Historical reports keep their old links.
"""
from pathlib import Path
import argparse
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs'
GENERATED = ROOT / 'lumigrid-lander-main/src/lib/generated'
API = ROOT / 'LumigridLedNode/components/factory/FactoryApi.cpp'


def read(path):
    raw = path.read_bytes()
    try:
        return raw.decode('utf-8-sig').replace('\r\n', '\n')
    except UnicodeDecodeError:
        # A few preserved historical records were originally Windows-1252.
        return raw.decode('cp1252').replace('\r\n', '\n')


def extract_routes(source):
    definitions = list(re.finditer(r'esp_err_t\s+(?:FactoryApi::)?(\w+)\(httpd_req_t\*\s*\w+\)\s*\{', source))
    bodies = {m[1]: source[m.end(): definitions[i + 1].start() if i + 1 < len(definitions) else len(source)]
              for i, m in enumerate(definitions)}
    routes = []
    for m in re.finditer(r'\.uri\s*=\s*"([^"]+)"\s*,\s*\.method\s*=\s*HTTP_(\w+)\s*,\s*\.handler\s*=\s*(\w+)', source):
        body = bodies.get(m[3], '')
        role = re.search(r'api::authorize\(request,\s*api::ApiRole::(\w+)\)', body)
        routes.append({'path': m[1], 'method': m[2], 'handler': m[3],
                       'access': role[1] if role else 'Handler-specific; inspect source',
                       'line': source.count('\n', 0, m.start()) + 1})
    if len(routes) < 50:
        raise ValueError('Unexpected route table: update the extractor before publishing.')
    if len({(r['method'], r['path']) for r in routes}) != len(routes):
        raise ValueError('Duplicate route registration')
    return routes


def capacity_facts():
    directory = ROOT / 'LumigridLedNode/components/config/include'
    specs = {
        'addressable': ('OutputCapabilities.hpp', 'kRmtLanes'),
        'pwm': ('OutputCapabilities.hpp', 'kPca9685Channels'),
        'pixels': ('SignageTypes.hpp', 'MAX_FIXTURE_PIXELS'),
        'scenes': ('SignageTypes.hpp', 'kMaxPresets'),
        'zones': ('Capabilities.hpp', 'kZones'),
        'layers': ('Capabilities.hpp', 'kLayersPerZone'),
        'timelineSteps': ('Capabilities.hpp', 'kTimelineSteps'),
        'cueLists': ('SignageTypes.hpp', 'kMaxCueLists'),
        'cuesPerList': ('SignageTypes.hpp', 'kMaxCuesPerList'),
        'effects': ('SignageTypes.hpp', 'kSignageEffectCount'),
    }
    values, sources = {}, {}
    for key, (file, symbol) in specs.items():
        match = re.search(r'\b' + symbol + r'\s*=\s*(\d+)\s*;', read(directory / file))
        if not match:
            raise ValueError(f'Cannot extract {symbol}; do not publish stale limits')
        values[key] = int(match[1])
        sources[key] = f'LumigridLedNode/components/config/include/{file}#{symbol}'
    return {'limits': values, 'sources': sources}


API_INTRO = '''# HTTP API reference

This inventory is generated from `LumigridLedNode/components/factory/FactoryApi.cpp`.
It lists registered methods, routes and handler guards, not guessed endpoints.
For request/response fields, read the named handler and its matching adapter in
`lumigrid-lander-main/src/lib/api/`. The original detailed reference is retained
as a [historical document](history/2026-10-21/API_REFERENCE.md), not the current contract.

## Transport and authorization

The node serves HTTP and WebSocket `/ws`. There is no registered SSE endpoint.
Normal protected routes require provisioning and an appropriate role. Browser
sessions use `Authorization: Bearer <sessionToken>`; automation uses `X-API-Key`.
No persistent credential belongs in a URL. See [Access](ACCESS.md).

Create Admin access once with POST `/api/v1/auth/bootstrap` and JSON
`{"adminToken":"<16–64 character private key>"}`. Login through POST
`/api/v1/auth/session` with `{"token":"<configured key>"}`. Use the returned
`sessionToken` for protected requests. POST `/api/v1/auth/ws-ticket` obtains a
single-use ticket for the WebSocket handshake. The browser implementation is
`src/lib/state/nodeStore.ts`; do not open a second competing connection.

## Common workflows

| Task | Contract / client owner |
| --- | --- |
| Read identity/capabilities/status | info, identity, capabilities, status; api/node.ts |
| Configure output topology | config GET, config/validate POST, config PUT; api/config.ts |
| Change networking | network/config GET/PUT and network/status GET; api/config.ts |
| Author/activate scenes | presets GET/PUT/DELETE and activate POST; api/scenes.ts |
| Live control/release | manual PUT/DELETE; api/manual.ts |
| Timeline/calendar | sequence and schedules GET/PUT; api/timeline.ts |
| Cue lists and show transport | cue-list, cue/control, show/control; api/cues.ts and showTransport.ts |
| Inspect output/memory/storage | diagnostics routes; api/diagnostics.ts |
| Update firmware | ota POST with raw application bytes; api/firmware.ts |
| Backup/restore | backup GET, restore/validate POST, restore POST; api/recovery.ts |
| Physical acceptance | factory/certification families; api/factory.ts, certification.ts |

Read current data before constructing edits. Preserve schema/version and
revision fields expected by the handler. Command receipts and jobs distinguish
acceptance from completion; observe status/snapshots and failures before calling
an operation applied. Preview frames and estimated power are not measurements.

Typical errors include 400 invalid payload, 401 authorization/session expiry,
403 provisioning/role rejection, 409 conflicting operation/state, 413 image size,
429 authentication throttling and 507 runtime memory admission. Exact JSON
`error` values and additional fields are handler-specific; clients must retain
the server response rather than turning every error into a connectivity message.

OTA is Admin-only, checks image/storage conditions and requires post-reboot
verification. `GET /api/v1/ota/status` includes `nodeId`, `targetBinding`,
`runningPartition`, `nextPartition`, `targetPartition` and `maxFirmwareBytes`.
When `targetBinding` is true, send `X-LumiGrid-Node-Id` with the selected node's
immutable identity; an invalid or mismatched value is rejected before image
reads or flash writes. Successful full receipts include `nodeId` and
`targetPartition` alongside the accepted version and command/job IDs.

Confirm an upload only when fresh identity and OTA status match its accepted
node, version and target partition, the running partition equals that target,
the state is `confirmed`, and no rollback is reported. An earlier same-version
confirmation, missing status or an incomplete receipt is not confirmation of
the new candidate. Older nodes without `targetBinding` use identity preflight
and readback without the new header; they do not provide server-side address
binding. Cancelling the browser operation stops further requests but does not
undo an already accepted update. See [Flash layout](FLASH_LAYOUT.md), including
the effective signed-OTA setting. Backup and restore are separate from credential
recovery; inspect validation/scope before applying a restore.

Hardware acceptance status advertises `evidenceBinding: "session_attempt_v1"`.
Bind start/test-start/abort to the observed `certificationId`; a new session
uses the previous status ID (empty before the first session). Result submissions
also send `testStartedMs` and `testAttempt` from that test's `startedMs` and
`attempts`. Mismatched bindings return 409 before mutation. Legacy requests
omitting all binding fields retain active-test guards but cannot reject every
delayed request for a later attempt. Automatic checks remain firmware-owned.

Only the active manual test waiting for evidence accepts an observation. Both
passed and failed instrument results need finite measurements and exact units;
zero is valid. Serial/note/unit limits are 31/95/7 UTF-8 bytes. Physical result
acceptance can leave `confirmationPending: true` until the owned runner stops
output; new work remains blocked and failed blackout becomes failed evidence.
`cancellationPending` similarly distinguishes accepted abort from completion.
Report timestamps are device uptime, and reporting-image metadata describes
the exporting image. See [Hardware acceptance](HARDWARE_CERTIFICATION.md).

## Registered routes

Role values below are extracted from direct `api::authorize` calls. Entries
marked handler-specific can use bespoke credential/ticket checks or be public;
the label is **not** a claim of anonymous access. The source handler remains
authoritative, including state-dependent guards.

| Method | Path | Direct role guard | Handler | Source line |
| --- | --- | --- | --- | --- |
'''


def check_links(path, text):
    failures = []
    for href in re.findall(r'\]\(([^)]+)\)', text):
        href = href.split('#')[0]
        if not href or re.match(r'^[a-z]+:', href) or href.startswith('/'):
            continue
        if not (path.parent / href).exists():
            failures.append(f'{path.relative_to(ROOT)}: missing link {href}')
    return failures


def main():
    check = argparse.ArgumentParser()
    check.add_argument('--check', action='store_true')
    args = check.parse_args()
    routes = extract_routes(read(API))
    api_doc = API_INTRO + ''.join(f"| {r['method']} | `{r['path']}` | {r['access']} | `{r['handler']}` | {r['line']} |\n" for r in routes)
    outputs = {DOCS / 'API_REFERENCE.md': api_doc}
    entries = {}
    failures = []
    for path in sorted(DOCS.rglob('*.md')):
        if 'superpowers' in path.parts:
            continue
        markdown = api_doc if path.name == 'API_REFERENCE.md' and path.parent == DOCS else read(path)
        slug = path.relative_to(DOCS).with_suffix('').as_posix()
        title = re.search(r'^#\s+(.+)', markdown, re.M)
        entries[slug] = {'title': title[1] if title else slug, 'markdown': markdown,
                         'historical': slug.startswith('history/')}
        if not slug.startswith('history/'):
            failures += check_links(path, markdown)
    failures += check_links(ROOT / 'README.md', read(ROOT / 'README.md'))
    outputs[GENERATED / 'documentation.json'] = json.dumps(entries, ensure_ascii=False, indent=2) + '\n'
    outputs[GENERATED / 'firmwareFacts.json'] = json.dumps(capacity_facts(), indent=2) + '\n'
    outputs[GENERATED / 'routes.json'] = json.dumps(routes, indent=2) + '\n'
    for path, content in outputs.items():
        if args.check:
            if not path.exists() or read(path) != content:
                failures.append(f'Stale generated file: {path.relative_to(ROOT)}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf-8')
    for failure in failures:
        print(failure)
    if failures:
        return 1
    print(f"Documentation {'checked' if args.check else 'synced'}: {len(entries)} guides, {len(routes)} routes, 10 source-backed capacities; current links valid.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
