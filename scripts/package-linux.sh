#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: package-linux.sh <build-directory> <output-directory> <x64|arm64> <version>" >&2
  exit 2
fi

build_dir=$1
output_dir=$2
release_arch=$3
version=${4#v}

: "${LINUXDEPLOY:?Set LINUXDEPLOY to the pinned linuxdeploy AppImage}"
: "${APPIMAGETOOL:?Set APPIMAGETOOL to the pinned appimagetool AppImage}"
: "${APPIMAGE_RUNTIME:?Set APPIMAGE_RUNTIME to the pinned type-2 runtime}"

case "$release_arch" in
  x64) appimage_arch=x86_64 ;;
  arm64) appimage_arch=aarch64 ;;
  *) echo "unsupported Linux release architecture: $release_arch" >&2; exit 2 ;;
esac

for executable in cmake file desktop-file-validate; do
  command -v "$executable" >/dev/null 2>&1 || {
    echo "$executable is required" >&2
    exit 1
  }
done
for executable in "$LINUXDEPLOY" "$APPIMAGETOOL"; do
  [[ -x "$executable" ]] || { echo "packaging tool is not executable: $executable" >&2; exit 1; }
done
[[ -f "$APPIMAGE_RUNTIME" ]] || { echo "AppImage runtime is missing: $APPIMAGE_RUNTIME" >&2; exit 1; }

qmake=${QMAKE:-qmake6}
qmake=$(command -v "$qmake" || true)
[[ -n "$qmake" && -x "$qmake" ]] || { echo "Qt 6 qmake is required" >&2; exit 1; }
"$qmake" -query QT_VERSION | grep -Eq '^6\.' || { echo "qmake must select Qt 6" >&2; exit 1; }

mkdir -p "$build_dir" "$output_dir"
build_dir=$(cd "$build_dir" && pwd)
output_dir=$(cd "$output_dir" && pwd)
app_dir="$build_dir/AppDir"

cmake -E remove_directory "$app_dir"
DESTDIR="$app_dir" cmake --install "$build_dir" --prefix /usr --config Release

desktop_file="$app_dir/usr/share/applications/net.cyberbrand.CyberSnapper.desktop"
icon_file="$app_dir/usr/share/pixmaps/net.cyberbrand.CyberSnapper.png"
desktop-file-validate "$desktop_file"
[[ -s "$icon_file" ]] || { echo "Linux application icon is missing" >&2; exit 1; }

qt_plugins=$("$qmake" -query QT_INSTALL_PLUGINS)
wayland_plugins=()
for plugin in libqwayland-egl.so libqwayland-generic.so; do
  [[ -f "$qt_plugins/platforms/$plugin" ]] && wayland_plugins+=("$plugin")
done
[[ ${#wayland_plugins[@]} -gt 0 ]] || {
  echo "Qt Wayland platform plugins are required" >&2
  exit 1
}
extra_platform_plugins=$(IFS=';'; echo "${wayland_plugins[*]}")

export APPIMAGE_EXTRACT_AND_RUN=1
export EXTRA_PLATFORM_PLUGINS="$extra_platform_plugins"
export EXTRA_QT_MODULES=waylandcompositor
export NO_STRIP=1
export QMAKE="$qmake"

"$LINUXDEPLOY" \
  --appdir "$app_dir" \
  --desktop-file "$desktop_file" \
  --icon-file "$icon_file" \
  --deploy-deps-only "$app_dir/usr/bin/cybersnapper-agent" \
  --deploy-deps-only "$app_dir/usr/bin/cybersnapper-cli" \
  --plugin qt

# Qt 6 uses qt.conf and AppRun's normal library/plugin discovery. The hook in
# the pinned plugin predates its Qt 6 fix and can override the platform theme.
rm -f "$app_dir/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"

required_files=(
  "$app_dir/usr/bin/CyberSnapper"
  "$app_dir/usr/bin/cybersnapper-agent"
  "$app_dir/usr/bin/cybersnapper-cli"
  "$app_dir/usr/lib/cybersnapper/runtime/node"
  "$app_dir/usr/share/cybersnapper/worker/main.cjs"
  "$app_dir/usr/plugins/platforms/libqxcb.so"
  "$app_dir/usr/plugins/sqldrivers/libqsqlite.so"
)
for required in "${required_files[@]}"; do
  [[ -s "$required" ]] || { echo "packaged file is missing: $required" >&2; exit 1; }
done

for plugin_group in wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
  find "$app_dir/usr/plugins/$plugin_group" -type f -name '*.so' -print -quit | grep -q . || {
    echo "linuxdeploy did not bundle Qt plugin group: $plugin_group" >&2
    exit 1
  }
done

browser=$(find "$app_dir/usr/share/cybersnapper/browsers" -type f \
  \( -name chrome -o -name headless_shell \) -print -quit)
[[ -n "$browser" ]] || { echo "bundled Playwright Chromium is missing" >&2; exit 1; }

case "$release_arch" in
  x64) architecture_pattern='x86-64|x86_64' ;;
  arm64) architecture_pattern='aarch64|ARM aarch64' ;;
esac
for binary in "${required_files[@]:0:4}" "$browser"; do
  file "$binary" | grep -Eq "$architecture_pattern" || {
    echo "wrong architecture in packaged binary: $(file "$binary")" >&2
    exit 1
  }
done

for binary in "$app_dir/usr/bin/CyberSnapper" "$app_dir/usr/bin/cybersnapper-agent" "$app_dir/usr/bin/cybersnapper-cli"; do
  if ldd "$binary" | grep -q 'not found'; then
    ldd "$binary" >&2
    echo "packaged executable has unresolved libraries: $binary" >&2
    exit 1
  fi
done

appimage="$output_dir/CyberSnapper-linux-$release_arch.AppImage"
ARCH="$appimage_arch" VERSION="$version" "$APPIMAGETOOL" \
  --runtime-file "$APPIMAGE_RUNTIME" "$app_dir" "$appimage"
chmod +x "$appimage"

env -u APPIMAGE_EXTRACT_AND_RUN "$appimage" --appimage-offset | grep -Eq '^[0-9]+$'
file "$appimage" | grep -Eq "$architecture_pattern"

archive="$output_dir/CyberSnapper-linux-$release_arch.tar.gz"
tar -C "$app_dir/usr" -czf "$archive" .

echo "Created $appimage"
echo "Created $archive"
