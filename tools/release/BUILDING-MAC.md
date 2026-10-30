# Building the macOS release (arm64)

The counterpart of `build-release.ps1`. Apple Silicon Mac, macOS 13+.

## One-time setup

```sh
xcode-select --install                      # Command Line Tools
brew install qt cmake ninja conan rapidjson # Qt 6.4.3+ with Charts (CI uses 6.9.3)
conan profile detect                        # first run only
```

`macdeployqt` comes with Qt. `dmgbuild` is installed into a throwaway venv by
the script (or used from `PATH` if present).

## Build the app

Same flags as the macOS job in `.github/workflows/build.yml`, minus the
universal build:

```sh
mkdir build && cd build
conan install .. -s build_type=Release -s arch=armv8 -o with_openssl3=True \
    --build=missing -c tools.cmake.cmaketoolchain:generator=Ninja
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCHATTERINO_NO_AVIF_PLUGIN=On -DUSE_PRECOMPILED_HEADERS=OFF ..
ninja
```

The result is `build/bin/Leafyrino.app`.

## Make the release

1. Set `MIST1X_VERSION` in `src/common/Mist1XVersion.hpp`, add the notes to
   `tools/release/Что нового.txt`, rebuild.
2. Run:

   ```sh
   tools/release/build-release-mac.sh --version 1.2.6 \
       --app build/bin/Leafyrino.app --out ~/releases/mac-1.2.6 \
       --merge-sums <the SHA256SUMS.txt that build-release.ps1 produced>
   ```

3. Upload to the **same GitHub release** as the Windows files:
   `ChatterinoByMist1X-<version>-macOS-arm64.dmg` and the merged
   `SHA256SUMS.txt` (replace the Windows-only one). The updater verifies the
   DMG against that file, so the DMG must have a line in it.

Without `--merge-sums` the script writes only `SHA256SUMS-mac.txt`; append its
line to the Windows `SHA256SUMS.txt` by hand.

## Signing

Ad hoc by default (`codesign -s -`): users allow the first launch once
(see `Первый запуск.txt`, which is inside the DMG). With an Apple Developer ID:

```sh
MAC_CODESIGN_IDENTITY="Developer ID Application: Name (TEAMID)" \
NOTARY_PROFILE=my-notary-profile tools/release/build-release-mac.sh ...
```

## How the update works

The app downloads the DMG, checks it against `SHA256SUMS.txt`, and starts a
helper (`Misc/update-mac.sh`, see `src/singletons/Updates.cpp`). After the app
quits the helper mounts the DMG, copies the new `.app` next to the old one,
checks its signature, swaps them and starts the new app. If anything fails the
old app stays. An app that is not in a writable folder or runs from the DMG
only opens the release page.
