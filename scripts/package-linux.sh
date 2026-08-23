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

for executable in cmake file desktop-file-validate timeout; do
  command -v "$executable" >/dev/null 2>&1 || {
    echo "$executable is required" >&2
    exit 1
  }
done
for executable in "$LINUXDEPLOY" "$APPIMAGETOOL"; do
  [[ -x "$executable" ]] || { echo "packaging tool is not executable: $executable" >&2; exit 1; }
done
[[ -f "$APPIMAGE_RUNTIME" ]] || { echo "AppImage runtime is missing: $APPIMAGE_RUNTIME" >&2; exit 1; }

real_qmake=${QMAKE:-qmake6}
real_qmake=$(command -v "$real_qmake" || true)
[[ -n "$real_qmake" && -x "$real_qmake" ]] || { echo "Qt 6 qmake is required" >&2; exit 1; }
"$real_qmake" -query QT_VERSION | grep -Eq '^6\.' || { echo "qmake must select Qt 6" >&2; exit 1; }

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

qt_plugins=$("$real_qmake" -query QT_INSTALL_PLUGINS)
release_plugins="$build_dir/release-qt-plugins"
cmake -E remove_directory "$release_plugins"
cmake -E copy_directory "$qt_plugins" "$release_plugins"

# CyberSnapper only uses the SQLite driver. The MySQL, Mimer, ODBC, and
# PostgreSQL drivers pull in server libraries the package does not ship and
# that linuxdeploy cannot always resolve. Curate a private copy rather than
# modifying the installed Qt SDK (which may be read-only or shared).
rm -f "$release_plugins"/sqldrivers/libqsqlmysql.so \
      "$release_plugins"/sqldrivers/libqsqlmimer.so \
      "$release_plugins"/sqldrivers/libqsqlodbc.so \
      "$release_plugins"/sqldrivers/libqsqlpsql.so

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
qmake="$script_dir/release-qmake-wrapper.sh"
export CYBERSNAPPER_REAL_QMAKE="$real_qmake"
export CYBERSNAPPER_RELEASE_PLUGINS="$release_plugins"

wayland_plugins=()
for plugin in libqwayland.so libqwayland-egl.so libqwayland-generic.so; do
  [[ -f "$release_plugins/platforms/$plugin" ]] && wayland_plugins+=("$plugin")
done
[[ ${#wayland_plugins[@]} -gt 0 ]] || {
  echo "Qt Wayland platform plugins are required" >&2
  exit 1
}
[[ -f "$release_plugins/platforms/libqoffscreen.so" ]] || {
  echo "Qt offscreen platform plugin is required for packaged GUI validation" >&2
  exit 1
}
platform_plugins=(libqoffscreen.so "${wayland_plugins[@]}")
extra_platform_plugins=$(IFS=';'; echo "${platform_plugins[*]}")

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
# Remove both the hook and the generated AppRun source line; leaving only one
# of those changes makes every AppImage fail before CyberSnapper starts.
rm -f "$app_dir/apprun-hooks/linuxdeploy-plugin-qt-hook.sh"
sed -i '\|linuxdeploy-plugin-qt-hook\.sh|d' "$app_dir/AppRun"

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

# Exercise AppRun and the bundled Qt platform plugin, not just the AppImage
# header. A valid offset can still hide a launcher that fails before main().
appimage_smoke="$build_dir/appimage-smoke"
cmake -E remove_directory "$appimage_smoke"
mkdir -p "$appimage_smoke/runtime" "$appimage_smoke/config" \
         "$appimage_smoke/data" "$appimage_smoke/cache"
chmod 700 "$appimage_smoke/runtime"
env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH \
  APPIMAGE_EXTRACT_AND_RUN=1 \
  QT_QPA_PLATFORM=offscreen \
  XDG_RUNTIME_DIR="$appimage_smoke/runtime" \
  XDG_CONFIG_HOME="$appimage_smoke/config" \
  XDG_DATA_HOME="$appimage_smoke/data" \
  XDG_CACHE_HOME="$appimage_smoke/cache" \
  CYBERSNAPPER_AGENT_SERVER="$appimage_smoke/agent.sock" \
  CYBERSNAPPER_DEFAULT_PROJECT="$appimage_smoke/project" \
  CYBERSNAPPER_UI_SCREENSHOT="$appimage_smoke/gui.png" \
  timeout 60 "$appimage"
[[ -s "$appimage_smoke/gui.png" ]] || {
  echo "AppImage GUI smoke test did not create a screenshot" >&2
  exit 1
}

archive="$output_dir/CyberSnapper-linux-$release_arch.tar.gz"
tar -C "$app_dir/usr" -czf "$archive" .

echo "Created $appimage"
echo "Created $archive"
