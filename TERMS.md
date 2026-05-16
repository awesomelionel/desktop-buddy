# Terms and Conditions

_Last updated: 2026-05-16_

These terms cover use of the **Claude Buddy / desktop-buddy** firmware in
this repository, including source code and pre-built binaries published via
GitHub Releases. By flashing or running the firmware you agree to these
terms. If you do not agree, do not flash or run it.

## 1. The firmware

This is a hobbyist, open-source firmware for the Adafruit Feather ESP32-S3
Reverse TFT. It is developed by volunteer contributors and is **not an
official Anthropic product**. "Claude" and "Claude Desktop" are products of
Anthropic; this firmware merely communicates with Claude Desktop over BLE
using a public protocol.

## 2. No warranty

The firmware is provided **"as is"** and **"as available"**, without
warranty of any kind, express or implied, including but not limited to
warranties of merchantability, fitness for a particular purpose, accuracy,
or non-infringement. You use it at your own risk.

In particular, the maintainers make no promise that the firmware:

- Will work with any given hardware revision.
- Will remain compatible with any given version of Claude Desktop.
- Is free of bugs, security defects, or data-loss conditions.
- Will continue to be developed or supported.

## 3. Hardware risk

Flashing firmware to a microcontroller, using a lithium battery, and running
custom code on hardware all carry inherent risks, including but not limited
to bricking the device, damaging the battery, or causing the device to
become unresponsive. You are responsible for:

- Verifying that the firmware is appropriate for your hardware.
- Following the manufacturer's safety guidance for your battery.
- Backing up anything you do not want to lose.

## 4. OTA updates

The firmware can pull updates from GitHub Releases over HTTPS when you
explicitly request a check or install. Updates are not automatic. Installing
an update will:

- Replace the firmware on the inactive OTA partition.
- Reboot the device into the new image.
- Mark the new image valid after a successful boot (ESP32 rollback).

The maintainers do not guarantee that any particular release will be stable
on your device. You may decline updates and continue running an older
release.

## 5. Third-party services

The firmware interacts with the following third-party services. Your use of
each is governed by that party's own terms and privacy policy. The
maintainers have no control over them and are not responsible for them.

- **Anthropic / Claude Desktop** — over BLE, on your local computer.
- **GitHub** — for release metadata and firmware downloads during OTA.
- **api.busaunty.com** — only if you enable the bus card and configure a
  bus stop. Optional.

## 6. Local network and access

The firmware exposes:

- A BLE service advertising as `Claude-XXXX`.
- An **unauthenticated HTTP server** on the local network when connected to
  Wi-Fi.
- An **open Wi-Fi access point** named `claude-buddy-XXXX` during
  provisioning.

You are responsible for operating the device on a network you trust and for
completing Wi-Fi provisioning promptly. The maintainers are not liable for
unauthorized access, configuration changes, or Wi-Fi credential disclosure
caused by running the device on an untrusted network.

## 7. Acceptable use

You agree not to use the firmware:

- To violate any law or any third party's rights.
- To interfere with, disrupt, or attack any network, service, or device you
  do not own or have permission to use.
- To circumvent the rate limits, terms of service, or technical protections
  of any third-party service the firmware connects to (including GitHub and
  the bus-arrivals API).
- To impersonate another person or device on a network.

## 8. Modifications and forks

You are free to read, study, modify, and redistribute the source code in
accordance with the project's open-source license, when one is published in
the repository. Forks and modified builds are your responsibility — do not
represent them as the upstream project's official builds.

## 9. Limitation of liability

To the maximum extent permitted by law, in no event shall the maintainers or
contributors of this project be liable for any direct, indirect, incidental,
special, consequential, or exemplary damages, including but not limited to
damages for loss of data, hardware damage, loss of profits, or business
interruption, arising out of or in connection with the firmware, even if
advised of the possibility of such damages.

## 10. No support obligation

Issues and pull requests on GitHub may or may not be answered. Nothing in
these terms obliges any contributor to provide support, bug fixes, security
patches, or future releases.

## 11. Changes to these terms

These terms may be updated at any time by changing this file in the
repository. Continued use of the firmware after a change constitutes
acceptance of the new terms. The "Last updated" date above reflects the
most recent revision.

## 12. Severability

If any provision of these terms is held to be unenforceable, the remaining
provisions remain in full force and effect.

## 13. Contact

For questions, open a GitHub issue at
`github.com/awesomelionel/desktop-buddy/issues`.
