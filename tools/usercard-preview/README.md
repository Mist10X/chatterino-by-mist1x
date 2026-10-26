# Offline usercard fixture

`Leafyrino-usercard-preview` is a separate executable for the real
`UserInfoPopup` and its existing `ChannelView`. It does **not** call the normal
Leafyrino `main`, `runGui`, `Application::initialize`, account loading, IRC,
EventSub, the network manager, Helix, IVR, or PLUS startup. Twitch account and
message data are synthetic and in memory. The only optional connection is an
explicit synthetic local Archive endpoint.

The preview is disabled in normal builds. Enable with
`-DLEAFYRINO_USERCARD_PREVIEW=ON`, then build `Leafyrino-usercard-preview`.
Keep its executable and DLLs in a new directory below `D:\devtools`. Never
copy an existing profile, plugins, credentials or a junction there. The helper
uses existing mock application headers without linking gmock.

For a genuine before comparison, a separate developer-only source snapshot can
be supplied with `-DLEAFYRINO_USERCARD_BASELINE_DIR=<snapshot directory>`. When
that directory contains `BaselineUserInfoPopup.cpp/.hpp`, an optional
`Leafyrino-usercard-baseline-preview` target builds the prior real layout with
the same synthetic data and a minimal offline seam. The snapshot is not part
of production sources, and the default build does not require it. Do not run
the new-layout responsive checks against the old layout.

The preview refuses to run outside `D:\devtools`, uses portable paths belonging
to that executable, disables settings saving, and places its temporary settings
inside `preview-temporary` below its own directory. Visible windows start on
the nonprimary monitor; a visible launch without that monitor is rejected.
Neither working `C:\Leafyrino` nor Chatterino+ is launched, closed or modified.

Example (after deploying the independent test EXE and its runtime):

```powershell
& 'D:\devtools\leafyrino-card-test\Leafyrino-usercard-preview.exe' `
  --archive-socket LeafyrinoArchive-test-card `
  --archive-dataset leafyrino-card-demo-v1 `
  --case a --width 960 --height 670
```

`--archive-socket` only accepts `LeafyrinoArchive-test-*`; the only accepted
dataset is the fixed synthetic `leafyrino-card-demo-v1` (also the default). Without an
endpoint the preview uses an intentionally unavailable test socket and does
not discover or fall back to the working Archive.

Scenarios use the same login (`archive_demo`) for UID `910000001` (A) and
`910000002` (B). The empty scenario uses UID `910000003`. A and B have 18 local
buffer messages; long has 5,000. Messages include Russian, English, a long
unbroken line and literal HTML-looking text, all rendered by the real message
renderer. The A/B/EMPTY/LONG buttons swap the same actual popup.
The A buffer includes the exact synthetic saved message `demo-30`, so the real
message menu can navigate to that saved row. The preview enables the production
message-menu builder but disables every top-level action except its `Архив`
submenu, which uses only the explicitly configured synthetic local endpoint.

`--scripted-checks --report <path> --quit-after 1500` checks A → B → A, empty
and long history against the actual ID label and actual `ChannelView`, then
checks both responsive orientations and verifies that the external network
manager was never initialized. The actual production UID filter is also given
mixed users sharing one login, an older login of the right UID, and a message
without a UID. Only the right UID, including its old login, may remain.
A passing JSON report contains 14 checks with
`ok: true`; a failed check makes timed execution exit with code 30.
Report metadata includes the actual DPI ratio, size and
synthetic settings path. It contains no account credentials.

`--auto-close` constructs the actual production auto-closing frameless popup.
Its deactivation/self-deletion is preserved; controls remain alive, as the real
main window would, and can be closed normally. After self-deletion the scenario
buttons are disabled rather than using a deleted card. Scripted checks in this
mode have 16 assertions, including the native-input container contract and real
Qt tabbar mouse handling. The report records the exact Qt runtime and raw mouse
tracking flags of tabs, tabbar, content page, profile scroll and its viewport.

For a visible Windows run, `--auto-close --native-hit-test-checks` sends
`WM_NCHITTEST` only to the preview's own HWND and requires HTCLIENT at both tab
centers, the content page and profile viewport. It does not inject input or
inspect another window. Native clicks are still checked separately on the
second monitor; offscreen Qt mouse events do not prove Windows hit testing.

`--pending-shutdown --quit-after 1000` creates a process-owned silent local
endpoint and leaves the card's hello request pending until the preview exits.
It exercises cancellation during shutdown without contacting any existing
Archive. The endpoint is removed by normal object destruction.

`--scale` changes the widget scale, which is distinct from OS DPI. To test Qt
high-DPI rendering, launch a separate preview process with its own
`QT_SCALE_FACTOR=1.5` environment and confirm the reported `devicePixelRatio`.
For headless execution, use a process-local `QT_QPA_PLATFORM=offscreen`; no
screenshots from an offscreen test are evidence of the user-visible screen.

`Run-OfflineChecks.ps1` reproduces both DPI runs, both baseline runs and shutdown
during a pending local request. It starts only the exact packaged preview
executables with child-only environment variables and no console window,
checks exit codes and JSON assertions, and writes individual logs plus
`SUMMARY.json`. A process that exceeds 20 seconds is reported as a failed test
with its PID; the runner never treats force termination as successful shutdown.

Archive protocol, malformed response, timeout, cancellation and dataset tests
belong to the local client/server test targets. The visual fixture is not a
substitute for those tests or for visual inspection on the second monitor.

## Explicit configuration for a normal Leafyrino profile

The normal client can use a separate, nonsecret endpoint file in its own
`Paths.settingsDirectory`: `archive-bridge.json`. For the intended portable
installation this is `C:\Leafyrino\Settings\archive-bridge.json`:

```json
{
  "protocol": "archive-card",
  "version": 2,
  "socket_name": "EXPLICIT_LOCAL_SOCKET_NAME",
  "dataset_id": "EXPLICIT_DATASET_ID"
}
```

Replace both placeholders with the explicitly agreed local Archive endpoint
and dataset identity. This file configures the client only; it neither starts
Archive nor creates its server-side dataset manifest. The server's
`data/archive-bridge.json` has a different v1 dataset-manifest schema.
The dataset ID is an identity check, not a password or server authentication.

The reader accepts only a regular, nonsymlink file of at most **4096 bytes**
(4 KiB), with protocol `archive-card` and numeric version `2`. The socket is
1–128 ASCII letters, digits, underscores, dots or hyphens. The dataset is
1–128 ASCII characters, starts with a letter or digit, and otherwise permits
letters, digits, underscores, dots, colons and hyphens. Missing, unreadable,
oversized, malformed or invalid files leave the integration disabled. There
is no endpoint discovery or fallback to a different dataset.

Existing explicit settings `/archive/socketName` and `/archive/datasetId`
have priority: if **either** is nonempty, that pair is used and the sidecar is
not read. An incomplete or invalid explicit pair fails closed; it does not
silently fall back to the sidecar. Use the sidecar to configure the bridge
without reading, rewriting or copying the account-bearing `settings.json`.
The reader never writes either file.

The independent QtCore/QtNetwork client run passed **52/52 checks**, including
17 sidecar checks using only a temporary directory: valid/missing/file type,
protocol/version/type errors, invalid endpoint fields, malformed JSON,
4097-byte rejection and acceptance at the 4096-byte boundary. This records
source/test capability, not installation status. The offline preview still
uses only its explicitly configured synthetic test endpoint.
