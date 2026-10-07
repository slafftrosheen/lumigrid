# LumiGrid access reference

| Situation | Access |
| --- | --- |
| First boot | Connect `LumiGrid-Setup`, Wi-Fi password printed on serial console (random per-device), open `http://192.168.4.1`. Create and confirm a new 16–64 character Admin Access Key. |
| Initial bootstrap | `POST /api/v1/auth/bootstrap` is state-gated: open only on unprovisioned nodes (`bootstrapAllowed == true`). No universal password, no MAC-derived secret. |
| Normal login | Enter the permanent Node Access Key in the dashboard; the node returns a browser session. |
| Recovery | Existing permanent keys and deliberate recovery policy apply. No universal setup authentication on provisioned nodes. |
| Factory reset | Physical BOOT/GPIO 0 hold for five seconds, then release to reboot. Erases access/configuration/network settings and restores genuine first boot. |

## Roles

- **Administrator:** controller ownership, network setup, access keys and maintenance.
- **Installer:** installation and configuration operations allowed by the node.
- **Operator:** normal show/lighting operation allowed by the node.

Administrator can change the Admin key, create/change Installer and Operator
keys, and disable Installer or Operator. All permanent keys require 16–64
characters. Rotation invalidates affected sessions; Admin rotation invalidates
all browser sessions. Secrets are never displayed after saving.

## Credentials and tokens

| Name | Meaning | Usage |
| --- | --- | --- |
| Access Key | Permanent role credential | Dashboard login or `X-API-Key` header for automation. Keep private. Never put it in a URL. |
| Session Token | Temporary browser token | `Authorization: Bearer <session-token>`. Memory/sessionStorage only. 30-minute idle timeout and eight-hour absolute timeout. |
| WS Ticket | Internal one-use WebSocket credential | Authenticated `POST /api/v1/auth/ws-ticket`; consumed on connection, expires after 15 seconds. The dashboard handles it internally. |

Bootstrap is a state-gated one-time commissioning path. Admin creation issues
the permanent Admin session in the same request. Once provisioned, bootstrap is
closed. Wi-Fi failure, reboot, configuration errors and OTA rollback do not reopen it.

For step-by-step setup, see [GETTING_STARTED.md](GETTING_STARTED.md).
