# Access and recovery

The node owns Operator, Installer and Admin roles, in ascending authority. Browser sessions use `Authorization: Bearer <session-token>`; machine clients use `X-API-Key: <configured-role-key>`. Persistent keys are not session tokens. Query strings and `X-Auth-Token` are not accepted for persistent credentials.

`POST /api/v1/auth/bootstrap` accepts `{"adminToken":"<private key>"}` with 16–64 characters, only while unprovisioned in FactoryFirstBoot or PhysicalRecovery state. Protected normal APIs require provisioning. Admin manages role credentials through `/api/v1/auth/credentials`.

`POST /api/v1/auth/session` accepts `{"token":"<configured key>"}`. GET inspects the session; DELETE logs out. Never publish real keys or returned tokens in logs or screenshots.

## Lifetime and storage

Eight session slots have a 30-minute idle timeout and eight-hour absolute lifetime. WebSocket tickets are single-use, expire after 15 seconds and have eight slots. Obtain one via `POST /api/v1/auth/ws-ticket`; `src/lib/state/nodeStore.ts` owns the browser connection.

Credentials are stored as SHA-256 digests with 16-byte salts. This does not encrypt transport: the node serves HTTP/WS. Use an appropriately isolated local network.

## Recovery

First boot prints a generated AP key and offers Admin creation. Do not infer a production key from screenshots or the development-credential `test` build.

The runtime watches GPIO0 after a three-second startup grace period. A button held at startup must first be released; a subsequent approximately five-second hold triggers the factory-reset/recovery path. This is a destructive action, not routine login. Preserve configuration and installation records first. Safe/recovery plans keep lighting off and retain API/mDNS.

Sources under `LumigridLedNode/`: `components/auth/AuthManager.cpp`, `components/auth/include/AuthManager.hpp`, `AuthTypes.hpp`, `components/api/ApiAuth.cpp`, `main/LumigridRuntime.cpp`. See [API reference](API_REFERENCE.md) for handler-specific roles.
