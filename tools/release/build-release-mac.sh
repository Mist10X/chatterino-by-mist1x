#!/bin/bash
# Builds the macOS release of Chatterino by Mist1X: a DMG and its checksum.
#
#   tools/release/build-release-mac.sh --version 1.2.5 \
#       --app <built Leafyrino.app> --out <new folder> \
#       [--qt <Qt prefix>] [--merge-sums <SHA256SUMS.txt of the Windows build>]
#
# The Windows counterpart is build-release.ps1. The result in <out>/dist:
#   ChatterinoByMist1X-<version>-macOS-arm64.dmg
#   SHA256SUMS-mac.txt   (only the DMG)
#   SHA256SUMS.txt       (with --merge-sums: the Windows lines plus the DMG; this
#                         is the file to upload, the updater reads it)
#
# Signing is ad hoc by default. For a Developer ID set MAC_CODESIGN_IDENTITY
# ("Developer ID Application: ..."); with NOTARY_PROFILE (a notarytool keychain
# profile) the DMG is also notarized and stapled.
set -euo pipefail

version=""; app=""; out=""; qt=""; merge=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) version="$2"; shift 2 ;;
        --app) app="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --qt) qt="$2"; shift 2 ;;
        --merge-sums) merge="$2"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ -n "$version" && -n "$app" && -n "$out" ]] || {
    echo "Usage: $0 --version X.Y.Z --app <Leafyrino.app> --out <folder> [--qt <prefix>] [--merge-sums <file>]" >&2
    exit 2
}

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
identity="${MAC_CODESIGN_IDENTITY:--}"
appname="Chatterino by Mist1X"
dmgname="ChatterinoByMist1X-$version-macOS-arm64.dmg"
# Built in a temporary folder: iCloud-synced places (Documents, Desktop) stamp
# attributes on folders that codesign refuses. Only dist/ goes to <out>.
work="$(mktemp -d /tmp/mist1x-mac-build.XXXXXX)"
trap 'rm -rf "$work"' EXIT
staging="$work/staging"
dist="$out/dist"
bundle="$staging/$appname.app"
plist="$bundle/Contents/Info.plist"

[[ ! -e "$out" ]] || { echo "Output folder already exists: $out" >&2; exit 1; }
[[ -d "$app" ]] || { echo "Not an app bundle: $app" >&2; exit 1; }
[[ "$(uname -m)" == "arm64" ]] || echo "Warning: not an Apple Silicon Mac; the DMG is named arm64." >&2

# The program compares itself with the releases by this number.
in_source="$(sed -n 's/.*MIST1X_VERSION "\([^"]*\)".*/\1/p' "$repo/src/common/Mist1XVersion.hpp")"
[[ "$in_source" == "$version" ]] || {
    echo "MIST1X_VERSION in src/common/Mist1XVersion.hpp is '$in_source', not '$version'. Change it and rebuild first." >&2
    exit 1
}

# The binary has to be arm64 only (the updater looks for the arm64 DMG).
exe="$app/Contents/MacOS/Leafyrino"
[[ -f "$exe" ]] || { echo "No Contents/MacOS/Leafyrino in $app" >&2; exit 1; }
archs="$(lipo -archs "$exe")"
[[ "$archs" == "arm64" ]] || { echo "Expected an arm64 binary, got: $archs" >&2; exit 1; }

macdeployqt="$(command -v macdeployqt || true)"
for candidate in "$qt/bin/macdeployqt" "$(brew --prefix qt 2>/dev/null)/bin/macdeployqt"; do
    [[ -x "$candidate" ]] && { macdeployqt="$candidate"; break; }
done
[[ -n "$macdeployqt" ]] || { echo "macdeployqt not found: pass --qt <Qt prefix>." >&2; exit 1; }

mkdir -p "$staging" "$dist"  # dist is created under <out> (new folder)

# 1. The app under its own name, with its own identity.
ditto --noextattr --norsrc --noqtn "$app" "$bundle"
set_plist() {  # key type value
    /usr/libexec/PlistBuddy -c "Set :$1 $3" "$plist" 2>/dev/null ||
        /usr/libexec/PlistBuddy -c "Add :$1 $2 $3" "$plist"
}
set_plist CFBundleName string "$appname"
set_plist CFBundleDisplayName string "$appname"
set_plist CFBundleIdentifier string "app.mist1x.chatterino"
set_plist CFBundleShortVersionString string "$version"
set_plist CFBundleVersion string "$version"
set_plist CFBundleLongVersionString string "$version"
set_plist LSMinimumSystemVersion string "13.0"
set_plist NSHighResolutionCapable bool true

# 2. Qt frameworks and plugins.
# Signing is done below, once, over the finished bundle. Homebrew splits Qt into
# several prefixes, so macdeployqt cannot resolve two optional plugins (PDF
# images, the on-screen keyboard); the program uses neither, so they go.
"$macdeployqt" "$bundle" -verbose=1 -no-codesign -libpath="$(brew --prefix)/lib" || true
rm -f "$bundle/Contents/PlugIns/imageformats/libqpdf.dylib" \
      "$bundle/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"
rm -rf "$bundle/Contents/Frameworks/QtPdf.framework" \
       "$bundle/Contents/Frameworks/QtVirtualKeyboard.framework" \
       "$bundle/Contents/Frameworks/QtVirtualKeyboardQml.framework"
# every library the bundle loads has to be inside it
while IFS= read -r -d '' lib; do
    id="$(otool -D "$lib" 2>/dev/null | tail -n +2)"
    if otool -L "$lib" 2>/dev/null | tail -n +2 | awk '{print $1}' | grep -vxF -- "$id" | grep -qE '^(/opt/homebrew|/usr/local|/Users/)'; then
        echo "Library outside the bundle used by: $lib" >&2
        exit 1
    fi
done < <(find "$bundle" -type f \( -name '*.dylib' -o -path '*/Contents/MacOS/*' -o -path '*.framework/Versions/A/Qt*' \) -print0)

# 3. Icon (from resources/icon.png), licences, guide,
#    release notes and the extra theme: the same files as the Windows package.
resources="$bundle/Contents/Resources"
if [[ "$(sips -g pixelWidth "$repo/resources/icon.png" | awk '/pixelWidth/ {print $2}')" -ge 256 ]]; then
    # the source is 256 px: sizes up to 256 are exact, nothing is scaled up
    iconset="$work/icon.iconset"
    mkdir -p "$iconset"
    for size in 16 32 128; do
        sips -z $size $size "$repo/resources/icon.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
        sips -z $((size * 2)) $((size * 2)) "$repo/resources/icon.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    done
    sips -z 256 256 "$repo/resources/icon.png" --out "$iconset/icon_256x256.png" >/dev/null
    iconutil -c icns "$iconset" -o "$resources/chatterino.icns"
fi
cp "$repo/LICENSE" "$resources/LICENSE.txt"
cp -R "$repo/LICENSES" "$resources/LICENSES"
cp -R "$repo/resources/licenses" "$resources/LICENSES/third-party"
cp "$repo/CREDITS.md" "$resources/Авторы и лицензии.txt"
cp "$here/Руководство.html" "$resources/"
cp "$here/Что нового.txt" "$resources/"
mkdir -p "$resources/Themes"
cp "$here/Mist-Dark.json" "$resources/Themes/"

# 4. Nothing from a profile may be in the package.
for name in Settings Cache Logs Misc Plugins ProfileAvatars IPC Crashes; do
    if [[ -n "$(find "$bundle" -name "$name" -print -quit)" ]]; then
        echo "Profile folder in the package: $name" >&2
        exit 1
    fi
done
bad="$(grep -rIlE 'oauth:|"oauthToken"|access_token' "$bundle" \
    --include='*.json' --include='*.txt' --include='*.ini' --include='*.log' || true)"
[[ -z "$bad" ]] || { echo "Token-like text in: $bad" >&2; exit 1; }

# 5. Signature (macdeployqt has just changed the bundle).
xattr -cr "$bundle"
if [[ "$identity" == "-" ]]; then
    codesign --force --deep --sign - "$bundle"
else
    codesign --force --deep --options runtime --timestamp --sign "$identity" "$bundle"
fi
codesign --verify --deep --strict "$bundle"

# 6. The disk image.
cp "$here/Первый запуск.txt" "$staging/Первый запуск.txt"
dmgbuild="$(command -v dmgbuild || true)"
if [[ -z "$dmgbuild" ]]; then
    python3 -m venv "$work/venv"
    "$work/venv/bin/pip" install --quiet dmgbuild
    dmgbuild="$work/venv/bin/dmgbuild"
fi
"$dmgbuild" -s "$here/dmg-settings-mac.py" \
    -D "app=$bundle" -D "readme=$staging/Первый запуск.txt" \
    "$appname" "$dist/$dmgname"
if [[ "$identity" != "-" ]]; then
    codesign --force --timestamp --sign "$identity" "$dist/$dmgname"
    if [[ -n "${NOTARY_PROFILE:-}" ]]; then
        xcrun notarytool submit "$dist/$dmgname" --keychain-profile "$NOTARY_PROFILE" --wait
        xcrun stapler staple "$dist/$dmgname"
    fi
fi

# 7. Checksums: "<hash>  <name>", the format the updater reads.
(cd "$dist" && shasum -a 256 "$dmgname" > SHA256SUMS-mac.txt)
if [[ -n "$merge" ]]; then
    { grep -v " $dmgname\$" "$merge" || true; cat "$dist/SHA256SUMS-mac.txt"; } |
        sed 's/ \*/  /' | sort -k2 > "$dist/SHA256SUMS.txt"
fi
ls -l "$dist"
echo "Done: $dist/$dmgname"
