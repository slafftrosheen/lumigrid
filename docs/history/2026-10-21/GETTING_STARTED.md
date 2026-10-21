> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

FIRST POWER-ON

1. Connect to Wi-Fi:
   LumiGrid-Setup

2. Password:
   See serial console on first boot (random per-device WPA2 key)

3. Open:
   http://192.168.4.1

4. Create your Admin Access Key.

5. Continue through the setup wizard.

## Secure your controller

The dashboard detects a new controller and opens First Setup. Choose a new
Admin Access Key of 16–64 characters and confirm it. You can also select
**Generate Secure Key** and save the generated key before continuing.
Setup saves the key and signs you in as Administrator in one action.

Wi-Fi password and Node Access Key are separate credentials in distinct security domains.
The setup AP password is a random per-device WPA2 secret printed on the serial console.
It is never derived from MAC or chip ID. The setup AP password does not grant Admin access.

## Normal login and accounts

Open the controller's address and enter your permanent Node Access Key.
The browser uses a temporary session; it does not save the permanent key.
Administrator can create/change Installer and Operator keys in **Access**
from the dashboard's system navigation. Keys must be 16–64 characters.
Saved secrets are never displayed. Keep your Admin key somewhere safe.

Installer accounts support installation/configuration within their permitted
role. Operator accounts support normal lighting operation. Administrator owns
networking, credentials and maintenance. See [ACCESS.md](ACCESS.md).

## Network commissioning

After creating the Admin key, continue through identity, network, outputs,
safety, hardware validation and backup. In the embedded dashboard, the setup
guide provides Wi-Fi setup and links to output configuration, diagnostics and
backup. Apply your Wi-Fi name and password only after securing the controller.
Network changes may disconnect the browser. Connect your computer to the selected
network, find the controller's new address through your router, and sign in
with your permanent key. Review reported network status; a queued change is not
proof that the controller joined Wi-Fi successfully.

## Physical recovery

Recovery is distinct from first setup. A provisioned controller keeps its
permanent keys during reboot, Wi-Fi loss, configuration errors and OTA rollback.
The existing recovery AP policy and recovery safeguards still apply; recovery
never grants the universal temporary setup key on a deployed controller.
Use your existing Admin key for maintenance. Recovery access does not clear
ownership. If the Admin key is lost, a physical full factory reset is required.

## Physical full factory reset

With power on, hold the controller's BOOT button (GPIO 0) for at least five
seconds, then release it so the controller can reboot. This erases credentials,
controller configuration and network settings. Back up configuration first
when possible. Only this deliberate physical reset restores the first-power-on
setup credentials above. An ordinary reboot does not reset access.
