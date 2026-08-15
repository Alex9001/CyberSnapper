# Packaging and publishing

This document describes *how* CyberSnapper's release packages are built and
published. For the step-by-step release *process* (rehearse → tag → publish →
verify), see [RELEASE.md](../RELEASE.md). For local builds and the install
tree, see [BUILDING.md](BUILDING.md).

Everything runs in one workflow: `.github/workflows/release.yml` ("Native
Release"). Each package is produced on a GitHub Actions runner for its target
platform, smoke-tested end to end, then attached to the GitHub release together
with checksums and provenance attestations.

## Package matrix

Every release ships an install-friendly package plus a portable archive for
each supported architecture:

| Platform | Architectures | Recommended | Portable | Runners |
| --- | --- | --- | --- | --- |
| Linux | x64, arm64 | AppImage | tar.gz | `ubuntu-24.04`, `ubuntu-24.04-arm` |
| Windows | x64, arm64 | NSIS setup `.exe` | ZIP | `windows-2022`, `windows-11-arm` |
| macOS | x64, arm64 | DMG | ZIP | `macos-15-intel`, `macos-15` |

Asset names follow `CyberSnapper-<platform>-<arch>.<ext>`, for example
`CyberSnapper-linux-x64.AppImage`, `CyberSnapper-windows-arm64-setup.exe`, and
`CyberSnapper-macos-x64.dmg`.

## Workflow shape

The workflow has three platform jobs plus a publish job:

- `linux` — matrix over x64 and arm64.
- `windows` — matrix over x64 and arm64.
- `macos` — matrix over x64 and arm64.
- `publish` — `needs` all three jobs; runs only when a release tag is involved.

Triggers:

- `release: [published]` — builds from the published release tag and attaches
  the packages to that release. This is the normal publication path.
- `workflow_dispatch` with an optional `release_tag` input — a blank tag is a
  *rehearsal* (packages are uploaded as workflow artifacts, no release is
  touched); a nonblank tag is the *recovery* path for rebuilding and
  re-attaching an existing release.

The `checkout` step selects `ref` from the release tag, the dispatch tag, or
the branch — in that order. The workflow definition itself is loaded from the
dispatched branch, while the built source comes from the selected ref.

## Common build steps

Every platform job starts with the same sequence:

1. **Version gate** — `scripts/check-release-version.mjs` asserts that
   `package.json` and `CMakeLists.txt` declare the same version, and that any
   requested tag equals `v<version>`. A tag/source mismatch fails the job
   before anything is built.
2. **Qt 6.8.3** — installed via `jurplel/install-qt-action`.
3. **Worker bundle** — `npm ci`, `typecheck:worker`, `build:worker`,
   `test:worker` produce `worker/dist/main.cjs` and its production
   `node_modules`.
4. **Bundled Chromium** — `npx playwright install chromium` into
   `.playwright-browsers/`, which the CMake install step copies into the
   package as the initial browser cache.
5. **Node runtime** — the runner's `node` (or `node.exe`) is staged into
   `.runtime/`, giving the package a private, pinned Node.
6. **Build and test** — CMake configure/build plus `ctest`.
7. **Production dependencies** — `npm prune --omit=dev` trims the worker
   `node_modules` before install.

The CMake install rules then lay out the application, agent, CLI, worker
bundle, worker dependencies, Node runtime, and browser cache. Two rules carry
`USE_SOURCE_PERMISSIONS` — the `.runtime/` Node binary and the
`.playwright-browsers/` Chromium — so their executables keep the executable bit
on POSIX systems; without it, Playwright cannot spawn the bundled browser.

## Linux: AppImage and tar.gz

`scripts/package-linux.sh <build-dir> <output-dir> <arch> <version>` drives the
package:

1. `cmake --install` into an `AppDir/` tree under the build directory.
2. The desktop file, metainfo, and icon are validated (these are installed by
   the CMake rules for Linux).
3. Unused Qt SQL drivers (`mysql`, `mimer`, `odbc`, `psql`) are removed; only
   SQLite is deployed. `libqsqlmimer.so` in particular depends on an absent
   `libmimerapi.so` and would otherwise abort deployment.
4. `linuxdeploy` with `linuxdeploy-plugin-qt` bundles non-Qt and Qt
   dependencies into `AppDir/`. A stale Qt 6 hook is removed after deployment.
5. `appimagetool` with a pinned type-2 runtime turns `AppDir/` into the
   `.AppImage`.
6. The portable archive is `tar -czf` of `AppDir/usr/`.

The script also guards the result: required files must exist, every binary must
match the target architecture, and `ldd` must report no unresolved libraries
before the AppImage is produced.

The four external tools — `linuxdeploy`, `linuxdeploy-plugin-qt`,
`appimagetool`, and the AppImage type-2 runtime — are **pinned by immutable
GitHub release-asset ID plus SHA-256** and downloaded with `gh api`. Pinning by
asset ID (not tag name) prevents a replaced "continuous" upstream asset from
silently changing what gets packaged. The per-architecture IDs and checksums
live in the workflow matrix.

## Windows: setup executable and portable ZIP

1. `cmake --install` stages the application into `stage/`.
2. `windeployqt` runs against both the GUI and the agent; unused SQL drivers
   are removed.
3. The **portable ZIP** is `Compress-Archive` of the staged tree.
4. The **setup executable** is compiled from `native/packaging/windows/installer.nsi`
   with NSIS `makensis`. The installer is per-user (installs under
   `%LOCALAPPDATA%\Programs\CyberSnapper`), writes Start Menu and desktop
   shortcuts, and registers an uninstaller.

`scripts/install-nsis.ps1` pins NSIS 3.12 and verifies the official installer
by SHA-256 before extracting `makensis.exe` under `RUNNER_TEMP`.

Windows arm64 cross-compiles with `ilammy/msvc-dev-cmd` (`amd64_arm64`) and the
`win64_msvc2022_arm64` Qt kit on a `windows-11-arm` runner.

## macOS: DMG and ZIP

1. A `.icns` is generated from `assets/logo.png` with `sips` + `iconutil` and
   passed to CMake as `CYBERSNAPPER_MACOS_ICON`.
2. `cmake --install` stages `CyberSnapper.app`.
3. `macdeployqt` bundles Qt for the app, agent, and CLI. A `brotli` helper
   library is copied in and its `@rpath` references are rewritten, and unused
   SQL drivers are removed.
4. The app bundle is **ad-hoc signed** (`codesign --force --deep --sign -`);
   it is not notarized. The **DMG** is created with `hdiutil` and the **ZIP**
   with `ditto`.

## Smoke test

`scripts/smoke-packaged-capture.mjs` is the gate between packaging and
publication. It takes six paths — CLI, agent, worker entry, Node runtime,
browser cache, and a scratch state root — and proves the *packaged* pieces work
together, not just the build tree:

1. It provisions an isolated config/data/cache tree and a short IPC runtime
   directory (Unix socket paths are length-limited).
2. It starts a local HTTP server serving a styled test page.
3. It runs `cybersnapper-cli --json projects open <dir>`, which cold-starts the
   packaged agent over the packaged IPC socket.
4. It runs a real `capture` with the Chromium engine and requires the job to
   reach `succeeded` and produce three responsive PNGs.
5. It stops the agent and tears down the server.

The worker, Node, and browser locations are injected through
`CYBERSNAPPER_AGENT`, `CYBERSNAPPER_WORKER_ENTRY`, `CYBERSNAPPER_NODE`, and
`CYBERSNAPPER_BROWSER_CACHE`. Each job strips `QT_PLUGIN_PATH` and
`QML2_IMPORT_PATH` so the packaged binaries resolve Qt from the package, not
from the runner's full Qt install.

Because this step runs before `upload-artifact`, a package that cannot actually
launch Chromium never reaches the release.

## Publishing

When all platform jobs succeed and a release tag is in play, the `publish` job:

1. Downloads all uploaded artifacts (merged into one directory).
2. Writes `SHA256SUMS.txt` from every asset.
3. Records a GitHub/Sigstore build-provenance attestation over the artifacts.
4. Uploads everything to the release with
   `gh release upload "$tag" artifacts/* --clobber`. The job has no checkout,
   so it sets `GH_REPO` to point `gh` at the repository explicitly.

## Signing

- macOS application bundles are ad-hoc signed but not notarized.
- Windows installers and Linux packages are unsigned.

Checksums and provenance attestations are the verification path for every
download. No paid signing identity is required.

## Recovery

If a package job fails after a tag is already published, re-dispatch the
workflow with the same nonblank `release_tag`; the build jobs check out that
tag and the publish job re-attaches with `--clobber`. Do not move a published
`v*` tag — ship a new patch version if tagged source needs a code change. See
the Recovery section of [RELEASE.md](../RELEASE.md) for the full policy.
