# Manage — OTA & Factory Reset — Design

Date: 2026-05-11
Status: Draft for review
Related: `src/net/HttpServer.{h,cpp}` (new endpoints), `src/core/Settings.{h,cpp}`
(NVS namespace target of factory reset), `src/core/ConfigStore.{h,cpp}` (NVS
namespace target of factory reset), `src/ui/CardController.{h,cpp}` (registers
new cards), `platformio.ini` (`FW_VERSION` build flag),
`.github/workflows/` (new release workflow).

Adds two operations to the existing web dashboard under a new **Manage**
section: pull-based firmware update from GitHub Releases, and
factory-reset-over-the-wire. This is the second of the three "what can the
user do on the web" sub-projects called out in
[configure-via-web](2026-04-30-configure-via-web-design.md). **Personalize**
(eyes customization) is still the third.

## Goals

- Install a new firmware version without USB once the device is on Wi-Fi.
- Manual-only check cadence: the device never polls GitHub on its own —
  only when the user clicks **Check for updates** in the web UI.
- Factory reset (wipe `claude-buddy` + `settings` NVS namespaces, reboot into AP) over the
  wire, gated on a physical hold of the center button so a misclick from
  another room cannot wipe the device.
- Bootloop-safe: a bad firmware reverts automatically on the next boot.
- TLS-validated download (no `setInsecure()`).
- Reusable OTA scaffolding for future Manage-style operations.

## Non-goals

- Background update polling. Manual only — see configure-via-web's cadence
  question; same answer here.
- Update channels (stable / beta / nightly). One release stream.
- Cryptographic signing of the `.bin`. TLS verifies transport; the trust
  anchor is "whoever can push tags to the GitHub repo can ship firmware."
  For a personal device this is acceptable.
- Authentication on the Manage section. Inherits the configure-via-web
  trust model (anyone on the LAN can hit it). A future `Lock` spec covers
  this.
- Rollback UI. ESP32 bootloader auto-rollback handles bootloop failures;
  no manual "revert to previous version" button.
- Multi-device fleet management or remote-initiated update push.
- Pre-release tag handling (`v1.0.0-rc1`, `-beta`). Pre-release suffixes
  are stripped and the base version compared. YAGNI; revisit if you start
  cutting RCs.

## Distribution model

Releases live on **GitHub Releases** at
`github.com/awesomelionel/desktop-buddy/releases`.

- Tag scheme: `vMAJOR.MINOR.PATCH` (`v1.0.0`, `v1.2.3`).
- One `.bin` asset per release: `firmware.bin`. The CI workflow guarantees
  this; the spec assumes it.
- Release `body` (markdown) is shown to the user as release notes before
  install. The device pulls it through and the web UI renders it.
- `api.github.com/repos/awesomelionel/desktop-buddy/releases/latest`
  returns the newest non-prerelease, non-draft release. That's what the
  device queries.

## Versioning & build stamping

The current firmware version is a `const char*` baked into the binary at
compile time:

```cpp
// src/core/firmware_version.h
constexpr const char* FIRMWARE_VERSION =
#ifdef FW_VERSION
    FW_VERSION;
#else
    "0.0.0-dev";
#endif
```

`platformio.ini` adds:

```ini
build_flags =
    ; ...existing flags...
    -DFW_VERSION='"0.0.0-dev"'   ; overridden by CI on tagged builds
```

CI strips the leading `v` from the tag and overrides `FW_VERSION` via a
generated `build_flags` line for that build only (see CI section below).

**Comparison rule.** Strict semver, three integer components.

| Current | Latest | Result |
|---|---|---|
| `1.2.3` | `1.2.3` | up-to-date |
| `1.2.3` | `1.2.4` | update available |
| `1.3.0` | `1.2.9` | up-to-date (never offer a downgrade) |
| `0.0.0-dev` | any tagged | update available (`-dev` parses to `0.0.0`) |
| `1.2.3-rc1` | `1.2.3` | update available (prerelease suffix stripped to `1.2.3`, both equal — *but* the prerelease is treated as "less than" the bare version so the bare wins) |

Parser is permissive: anything not matching `^v?(\d+)\.(\d+)\.(\d+)` falls
back to `0.0.0` so a malformed tag never blocks comparison.

## Partition table — already OTA-capable

Good news: the existing `min_spiffs.csv` (bundled with the ESP32 Arduino
core, currently set as `board_build.partitions`) is already a dual-OTA
layout:

```
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x5000
otadata,  data, ota,     0xe000,  0x2000
app0,     app,  ota_0,   0x10000, 0x1E0000     # 1.875 MB
app1,     app,  ota_1,   0x1F0000,0x1E0000     # 1.875 MB
spiffs,   data, spiffs,  0x3D0000,0x20000      # 128 KB (unused today)
coredump, data, coredump,0x3F0000,0x10000      # 64 KB
```

No partition-table change is needed. The first firmware that supports OTA
*can itself* be installed over USB once, and every release after that
flows over the wire — no awkward "this build must be flashed via USB,
future builds will be wireless" caveat.

Current firmware is well under 1 MB; both app slots have generous
headroom for the OTA scaffolding plus the Mozilla CA bundle (~150 KB).

## Components

### `core/UpdateManager.{h,cpp}` (new)

Owns the OTA state machine. Singleton, ticked from `loop()`.

```cpp
enum class State : uint8_t {
    Idle,
    Checking,            // GitHub API request in flight
    UpToDate,            // last check succeeded, no newer version
    UpdateAvailable,     // last check succeeded, latest > current
    Downloading,         // install in progress
    InstallReady,        // download complete, about to reboot
    Failed,              // last operation failed; see last_error
};

struct ReleaseInfo {
    char     tag[16];               // "1.2.3" (no leading v)
    char     download_url[256];     // firmware.bin asset URL
    char     body[1024];            // release notes (markdown, truncated)
};

struct UpdateStatus {
    State    state;
    uint32_t bytes_received;        // valid in Downloading
    uint32_t bytes_total;           // valid in Downloading (from Content-Length)
    char     last_error[64];        // valid in Failed
};

class UpdateManager {
public:
    void begin();                   // no-op for cert setup (cert bundle is global)
    void tick(uint32_t now_ms);     // drives async download progress

    void requestCheck();            // called from HttpServer
    void requestInstall();          // called from HttpServer; requires UpdateAvailable

    UpdateStatus       status() const;
    const ReleaseInfo* latestRelease() const;   // nullptr unless we've checked
};
```

Emits `EventKind::UpdateStateChanged` on every state transition so the
`CardController` can swap to `UpdatingCard` when entering `Downloading`.

### `net/GitHubReleases.{h,cpp}` (new)

Thin HTTPS client used only by `UpdateManager`.

```cpp
struct LatestReleaseResponse {
    bool        ok;
    char        error[64];          // populated when !ok
    ReleaseInfo info;               // populated when ok
};

LatestReleaseResponse fetchLatestRelease();
```

- Uses `WiFiClientSecure` with `setCACertBundle(rootca_crt_bundle_start)`
  (the Arduino-ESP32 v3.x global Mozilla bundle, ~150 KB in flash).
- Hits `https://api.github.com/repos/awesomelionel/desktop-buddy/releases/latest`.
- Parses JSON with the existing ArduinoJson (already a dep).
- Extracts `tag_name`, `body`, and `assets[0].browser_download_url` (where
  the asset's `name == "firmware.bin"`). Picking by name guards against
  CI publishing extras in the future.
- Read timeout 10 s, total budget ~15 s.

### `ui/cards/UpdatingCard.{h,cpp}` (new)

Full-screen takeover during `Downloading` and `InstallReady`. Layout:

```
        UPDATING
       ─────────────
       v1.2.3 → v1.3.0

       ████████░░░░░░░░  47%

       Do not unplug
```

Reads `UpdateStatus` from `UpdateManager`. Dirty-tracked by
`bytes_received` (5 KB granularity to avoid every-frame redraws — see
CLAUDE.md re: `fillScreen`). Reuses the existing `Footer` for context.

Force-shown via `CardController` when state enters `Downloading`. Cannot
be dismissed by button input — the device is unsafe to touch.

### `ui/cards/FactoryResetCard.{h,cpp}` (new)

Full-screen takeover when armed by `POST /api/factory-reset`. Layout:

```
       FACTORY RESET
       ─────────────
       Hold center 3s
       to confirm.

       Release to cancel.

       (will wipe Wi-Fi
        and all settings)
```

When the center button is held, a ring around the screen fills clockwise
in proportion to `hold_ms / 3000`. At 3000 ms the wipe fires.

### `core/FactoryResetCoordinator.{h,cpp}` (new)

Small state machine independent of UpdateManager.

```cpp
enum class Phase : uint8_t {
    Idle,
    AwaitingHold,        // armed via web POST; FactoryResetCard up
    Resetting,           // hold confirmed; wiping NVS, brief "Reset complete"
};

class FactoryResetCoordinator {
public:
    void tick(uint32_t now_ms, const InputState& input);
    void arm(uint32_t now_ms);      // called from HttpServer

    Phase   phase() const;
    uint32_t holdMs() const;        // for FactoryResetCard ring fill
};
```

`arm()` starts a 30-s window. In `AwaitingHold`, the coordinator watches
the center button via `InputState`:

- Press → start counting hold.
- Continuously held → `holdMs()` increments.
- Reach 3000 ms held → enter `Resetting`, call `ConfigStore::clear()`
  (wipes the `claude-buddy` namespace) and `Settings::clearToDefaults(...)`
  (wipes the `settings` namespace; default device name is regenerated from
  the MAC suffix), show "Reset complete" for 1 s, then `ESP.restart()`
  (boots into AP).
- Released before 3000 ms → return to `Idle`, dismiss card.
- 30-s window elapses without any hold completing → return to `Idle`,
  dismiss card.

### Coexistence with the existing 5-s long-press

Today, center-hold for 5 s clears the `claude-buddy` (Wi-Fi creds) namespace and reboots. That stays
exactly as it is — the recovery path for "I can't get to the web UI."

The new 3-s factory-reset hold is **only active when the coordinator is
in `AwaitingHold`** (i.e., the user explicitly armed it from the web).
Outside that window, holding center 5 s still does the wifi-only reset.
Inside the window, the 3-s threshold fires first; the existing
5-s handler observes the device has already rebooted and is a no-op.

Implementation note: the coordinator's `tick()` runs before the existing
long-press handler in the main loop so the 3-s threshold pre-empts the
5-s one inside the armed window.

## HttpServer endpoints

| Method+Path | Body | Purpose | Response |
|---|---|---|---|
| `GET /api/firmware-version` | – | Current build version. | `{"version": "1.2.3"}` |
| `POST /api/check-for-updates` | – | Triggers a check. Synchronous (~3–5 s). | `{"state": "update_available", "current": "1.2.3", "latest": "1.3.0", "notes": "..."}` or `{"state": "up_to_date", "current": "1.2.3"}` or `{"state": "failed", "error": "..."}` |
| `POST /api/install-update` | – | Begins install. Async. Requires `state == UpdateAvailable`; returns 409 otherwise. | `{"state": "downloading"}` |
| `GET /api/update-status` | – | Poll target during install. | `{"state": "downloading", "bytes_received": 524288, "bytes_total": 1100000}` then connection drops on reboot |
| `POST /api/factory-reset` | – | Arms the coordinator. Always returns 200 once armed; the actual wipe is gated on the button hold. | `{"state": "awaiting_hold", "timeout_s": 30}` |

`POST /api/check-for-updates` is synchronous because the GitHub API call is
fast enough (10-s ceiling) that holding the HTTP connection is simpler than
adding a second polling endpoint. If the call is slow the web UI shows a
spinner.

`POST /api/install-update` is async because the download is multi-second
and we don't want to block the HTTP server. The handler kicks
`UpdateManager::requestInstall()` and returns immediately; the actual
streaming runs in `UpdateManager::tick()`.

## OTA install internals

The download is streamed directly into Arduino's `Update` API via the
`HTTPUpdate` library (already part of the ESP32 Arduino core, no new
dependency):

```cpp
WiFiClientSecure client;
client.setCACertBundle(rootca_crt_bundle_start);

httpUpdate.onProgress([](int cur, int total) {
    UpdateManager::instance().onProgress(cur, total);
});
httpUpdate.rebootOnUpdate(false);   // we control the reboot

t_httpUpdate_return result =
    httpUpdate.update(client, latest_.download_url, FIRMWARE_VERSION);

if (result == HTTP_UPDATE_OK) {
    state_ = State::InstallReady;
    delay(500);   // let HttpServer flush in-flight response
    ESP.restart();
} else {
    state_ = State::Failed;
    strncpy(last_error_, httpUpdate.getLastErrorString().c_str(),
            sizeof(last_error_));
}
```

`HTTPUpdate` handles redirects (the GitHub asset URL 302s to
`objects.githubusercontent.com`) and the `Update.h` chunked write protocol
internally, which is the win over rolling our own.

On the very next successful boot after an OTA install, `setup()` calls
`esp_ota_mark_app_valid_cancel_rollback()` once the main loop has run a
few hundred milliseconds without crashing. If we crash before that, the
bootloader reverts to the previous slot on next power-up. This is the
automatic rollback path.

## Web UI — Manage section

Added at the bottom of the existing dashboard `/`, below `DANGER`. New
section block:

```
MANAGE     Current version: 1.2.3
                                                 [ Check for updates ]

           (after check, if up-to-date:)
           ✓ You're up to date.

           (after check, if available:)
           New version: 1.3.0
           ┌──────────────────────────────┐
           │ Release notes (markdown)     │
           │ rendered here…               │
           └──────────────────────────────┘
                                                 [ Install update ]

           (during install:)
           Installing 1.3.0…
           ████████░░░░  47%
           The device will reboot when finished.

                                                 [ Factory reset ]
           (after click:)
           Go to your device and hold the center
           button for 3 seconds to confirm.
           (Auto-cancels in 30 s.)
```

JS polls `/api/update-status` every 1 s during install. When polling fails
(device rebooted), it switches to polling `/api/firmware-version` every
2 s; on a successful response with the new version, it shows
"Updated to 1.3.0".

Release notes are rendered as **plain text with line breaks**. No markdown
parser on the device-side (we just pass the body string through) and no
markdown parser in the web UI either. YAGNI for v1; the body is the
release-notes text as-is. If it gets hard to read later we can layer
markdown rendering on top.

## Failure modes

| Failure | Behavior |
|---|---|
| No Wi-Fi when "Check" pressed | `/api/check-for-updates` returns 503 `{"error": "offline"}`. Web shows "Not connected to Wi-Fi." |
| GitHub API unreachable / 5xx | `state = Failed`, error stored. Web shows "Couldn't reach GitHub. Try again." |
| GitHub API rate-limited (60 req/hr unauth) | Same as above with a hint about retrying. Single-device usage will not realistically hit this. |
| Latest release has no `firmware.bin` asset | `state = Failed`, "No firmware in this release." Implies a CI failure. |
| Download interrupted mid-stream (Wi-Fi drop, GitHub 5xx) | `Update.end(false)` rolls back; partition unchanged. `state = Failed`, "Download failed — try again." Running image is untouched. |
| Image fails final verification | Same — `state = Failed`, "Image invalid." Running image untouched. |
| New image installs but bootloops | ESP32 bootloader reverts to previous slot on next power-up. User sees the old version after a power cycle. |
| Factory reset armed but no hold within 30 s | Coordinator disarms; `FactoryResetCard` dismisses; previous card returns. |
| Factory reset hold released early | Same — disarm, dismiss, no partial wipe possible (we only write to NVS after the full 3-s hold). |
| Web UI loses connection during install | JS retry loop switches from `/api/update-status` to `/api/firmware-version` polling and recovers when the device boots back up. |

## CI / release workflow

New file `.github/workflows/release.yml`:

- Trigger: `push` events with `tags: ['v*.*.*']`.
- Steps:
  1. Checkout.
  2. Install PlatformIO (cached).
  3. Extract version: `VERSION=${GITHUB_REF_NAME#v}`.
  4. Build: `pio run --environment adafruit_feather_esp32s3_reversetft
     --project-option="build_flags=-DFW_VERSION='\"$VERSION\"'"`.
     (Exact form: write a `pio.local.ini` so the flag isn't shell-quoting-
     dependent. The plan will nail the precise syntax.)
  5. Upload `.pio/build/.../firmware.bin` as a release asset (using `gh
     release upload "$GITHUB_REF_NAME" firmware.bin`).

The workflow does not auto-create a release — the developer creates the
release on GitHub (with body / release notes), then the workflow runs on
the tag push and attaches the binary. (Alternative: create-release-on-tag
+ require-empty-body-edit-after; not worth the complexity for v1.)

## Testing

- **Native unit tests** (existing `native` env):
  - `core/version_compare` (semver parse + compare).
  - `net/GitHubReleases` JSON parser given fixture responses.
  - `core/UpdateManager` state-machine transitions with a mocked release
    fetcher and a mocked install routine.
  - `core/FactoryResetCoordinator` arm → hold → confirm; arm → release →
    cancel; arm → timeout.
- **On-device manual checklist** (call out in plan):
  - Happy path: tag a v0.0.1 release, install over OTA, verify version
    bumps on reconnect.
  - Wi-Fi drop mid-download: airplane mode the laptop AP, confirm
    `Failed` state and untouched running image.
  - Bricking build: deliberately push a build that crashes in `setup()`,
    install it, confirm bootloader auto-rollback on next power-up.
  - Factory reset confirm path.
  - Factory reset cancel-by-release path.
  - Factory reset 30-s timeout path.
  - 5-s long-press still works outside the armed window.
- **Web smoke** (`tools/web-smoke.sh`):
  - `/api/firmware-version` returns valid JSON.
  - `/api/check-for-updates` returns one of the three documented states.
  - `/api/factory-reset` arms and returns the documented response. (Don't
    run the hold step in smoke — it would actually wipe the test device.)

## File layout

```
src/
  core/
    UpdateManager.{h,cpp}           # NEW
    FactoryResetCoordinator.{h,cpp} # NEW
    firmware_version.h              # NEW
    version_compare.{h,cpp}         # NEW (semver parse + compare)
  net/
    GitHubReleases.{h,cpp}          # NEW
    HttpServer.{h,cpp}              # MODIFIED — 5 new endpoints
  ui/
    cards/
      UpdatingCard.{h,cpp}          # NEW
      FactoryResetCard.{h,cpp}      # NEW
    CardController.{h,cpp}          # MODIFIED — register new cards
  main.cpp                          # MODIFIED — wire UpdateManager,
                                    #   FactoryResetCoordinator,
                                    #   esp_ota_mark_app_valid call

platformio.ini                      # MODIFIED — FW_VERSION build flag

.github/
  workflows/
    release.yml                     # NEW

test/
  native/
    test_version_compare/           # NEW
    test_github_releases_parse/     # NEW
    test_update_manager/            # NEW
    test_factory_reset_coordinator/ # NEW

tools/
  web-smoke.sh                      # MODIFIED — new endpoint checks
```

## Open questions for the implementation plan

- Exact `pio` invocation that passes `FW_VERSION` cleanly from a CI env var
  (shell-quoting around the `"` is the fiddly part — either `--build-flag`
  or a generated `pio.local.ini`).
- Whether `UpdatingCard` dirty-tracking should be 5 KB-granularity or a
  fixed cadence (e.g., 250 ms) — the CLAUDE.md `fillScreen` constraint
  applies; the plan should pick.
- Whether the GitHub workflow runs on `release: published` (developer
  manually publishes the release first) or on `push: tags v*.*.*` (the
  tag triggers the build). Personal preference and a 5-line yaml
  difference; the plan picks one.
