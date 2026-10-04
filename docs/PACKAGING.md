# Packaging and publishing

This document describes *how* CyberSnapper's release packages are built and
published. For the step-by-step release *process* (rehearse → tag → stage →
publish → verify), see [RELEASE.md](../RELEASE.md). For local builds and the install
tree, see [BUILDING.md](BUILDING.md).

Everything runs in one workflow: `.github/workflows/release.yml` ("Native
Release"). Each package is produced on a GitHub Actions runner for its target
platform, smoke-tested end to end, then staged on a draft GitHub release together
with checksums and provenance attestations. Publication is a separate explicit
action after the complete draft passes verification.

## Package matrix

Every release ships an install-friendly package plus a portable archive for
each supported architecture:

| Platform | Architectures | Recommended | Portable | Runners |
| --- | --- | --- | --- | --- |
| Linux | x64, arm64 | AppImage | tar.gz | `ubuntu-22.04`, `ubuntu-24.04-arm` |
| Windows | x64, arm64 | NSIS setup `.exe` | ZIP | `windows-2022`, `windows-11-arm` |
| macOS | x64, arm64 | DMG | ZIP | `macos-15-intel`, `macos-15` |

Asset names follow `CyberSnapper-<platform>-<arch>.<ext>`, for example
`CyberSnapper-linux-x64.AppImage`, `CyberSnapper-windows-arm64-setup.exe`, and
`CyberSnapper-macos-x64.dmg`.

## Workflow shape

The workflow has native package jobs and independent validation gates:

- `linux` — matrix over x64 and arm64.
- `windows` — matrix over x64 and arm64.
- `macos` — matrix over x64 and arm64.
- `appimage-compatibility` — audits the exact x64 AppImage and opens its GUI on Ubuntu 22.04 and 24.04 without a Qt SDK.
- `appimage-catalog` — runs the pinned upstream AppImageHub worker on the exact x64 candidate.
- `publish` — waits for every package and compatibility gate, validates the complete asset set, then preserves a rehearsal bundle or stages an attested draft.

Trigger:

- `workflow_dispatch` with an optional `release_tag` input. A blank tag is a
  rehearsal: packages are uploaded as workflow artifacts, no release is touched.
- A nonblank tag stages packages on a draft only. Dispatch at that exact tag,
  for example `gh workflow run release.yml --ref v2.4.2 -f release_tag=v2.4.2`.
  The final job rejects a mismatched workflow ref, commit, tag, or version.
- There is no `release: published` trigger. Publishing an already verified draft
  neither rebuilds nor overwrites its assets.

Every candidate checkout uses `github.sha`, including the independent runtime,
catalog, and assembly gates. The catalog worker retains its separate pinned
upstream commit. Staging runs for one tag are serialized without canceling an
active run. Maintainers must wait for successful staging before publishing;
GitHub has no atomic "upload only if still draft" operation.

## Common build steps

Every platform job starts with the same sequence:

1. **Version gate** — `scripts/check-release-version.mjs` asserts that
   `package.json`, `package-lock.json`, `CMakeLists.txt`, desktop metadata, AppStream, and the website declare the same version, and that any
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

After CMake copies dependencies into the release install tree, `scripts/prune-worker-dependencies.mjs` retains only its matching native Sharp/libvips variants (Windows bundles libvips in Sharp), preserves the optional WASM fallback, and verifies that the native module itself loads. PNG and AVIF round-trips must succeed from the staged tree, without resolving modules from the source checkout. This prevents npm optional dependencies for another architecture or libc from entering a release.

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
3. Qt plugins are copied into a private staging directory, where unused SQL
   drivers (`mysql`, `mimer`, `odbc`, `psql`) are removed; only SQLite is
   deployed. The installed Qt SDK is never modified. `libqsqlmimer.so` in
   particular depends on an absent `libmimerapi.so` and would otherwise abort
   deployment.
4. `linuxdeploy` with `linuxdeploy-plugin-qt` bundles non-Qt and Qt
   dependencies into `AppDir/`. A stale Qt 6 hook is removed after deployment.
5. `appimagetool` with a pinned type-2 runtime turns `AppDir/` into the
   `.AppImage` with architecture-specific update information and a matching `.zsync` file. Header, content hash, block table, and full offline zsync reconstruction must validate.
6. The portable archive is `tar -czf` of `AppDir/usr/`.

The script also guards the result: required files must exist, every binary must
match the target architecture, and `ldd` must report no unresolved libraries
before the AppImage is produced.

The four external tools — `linuxdeploy`, `linuxdeploy-plugin-qt`,
`appimagetool`, and the AppImage type-2 runtime — are downloaded from
**versioned upstream releases and pinned by SHA-256**. The workflow never uses
moving `continuous` assets or repository-hosted binary mirrors. Tool AppImages
run with `APPIMAGE_EXTRACT_AND_RUN=1`, so the build does not depend on FUSE.
The per-architecture checksums live in the workflow matrix.

Linux x64 builds on Ubuntu 22.04 to retain a lower glibc floor. Qt's official
Linux arm64 package requires Ubuntu 24.04, so the arm64 AppImage has that newer
glibc compatibility floor.

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
4. It runs `browsers install chromium --wait` against the packaged worker and
   bundled browser cache. This is a no-network install when the package is
   correct, but still exercises Playwright CLI discovery, progress plumbing,
   and post-install launch verification.
5. It runs a real `capture` with the Chromium engine and requires the job to
   reach `succeeded` and produce three responsive PNGs.
6. It stops the agent and tears down the server.

The worker, Node, and browser locations are injected through
`CYBERSNAPPER_AGENT`, `CYBERSNAPPER_WORKER_ENTRY`, `CYBERSNAPPER_NODE`, and
`CYBERSNAPPER_BROWSER_CACHE`. Each job strips `QT_PLUGIN_PATH` and
`QML2_IMPORT_PATH` so the packaged binaries resolve Qt from the package, not
from the runner's full Qt install.

Because this step runs before `upload-artifact`, a package that cannot actually
launch Chromium never reaches the release.

## Draft staging and publication

When all platform jobs succeed and a release tag is in play, the `publish` job:

1. Downloads the exact run's platform artifacts into one directory.
2. Requires exactly twelve nonempty native packages plus two matching AppImage
   zsync sidecars, then writes `SHA256SUMS.txt` from all fourteen assets.
3. Verifies the exact dispatch ref/commit, source version, tag target, local
   checksums and sidecars, and any existing draft assets before attesting.
4. Records GitHub/Sigstore build-provenance attestations over all fifteen files.
5. Runs `scripts/stage-draft-release.py` with the same tag and commit. The helper
   creates or resumes a draft, uploads only missing files without `--clobber`,
   and checks every remote name, state, size, and SHA-256 digest. It rechecks
   draft status before each upload and after verification. It refuses published
   releases, conflicting/incomplete assets, and a moved tag.

The job retains its existing `contents: write`, `id-token: write`, and
`attestations: write` permissions; it needs no additional secret or token scope.
Nothing in the workflow publishes a release. A maintainer explicitly changes
only the verified draft's publication state after the run succeeds; the same
attested bytes then become public. See [RELEASE.md](../RELEASE.md).

## Signing

- macOS application bundles are ad-hoc signed but not notarized.
- Windows installers and Linux packages are unsigned.

Checksums and provenance attestations are the verification path for every
download. No paid signing identity is required.

## Recovery

Resume a failed draft-staging job within the same run to reuse its exact retained
artifacts. Matching files are skipped; missing files are uploaded. A conflicting
or incomplete remote asset fails safely without deletion, so any cleanup needs
explicit review. Do not rerun packaging and assume its bytes will be identical,
and do not publish while staging is active. Published assets cannot be changed
by this workflow; use a new patch version for a defective public release. Never
move a published `v*` tag. See [RELEASE.md](../RELEASE.md) for the full policy.

## AppImage updates and catalog submission

Each AppImage embeds `gh-releases-zsync|Alex9001|CyberSnapper|latest|CyberSnapper-linux-<arch>.AppImage.zsync`, where `<arch>` is `x64` or `arm64`. The exact architecture-specific filename keeps external updaters from switching architectures. Prerelease packaging uses its exact version tag instead of the stable `latest` channel. The adjacent sidecar names the AppImage by basename, so both files can move together between staging and GitHub Releases. Checksums and provenance include the sidecars.

Use an AppImageUpdate-compatible external tool with CyberSnapper closed. The application does not silently update itself. Images older than 2.4.2 lack update information and need a one-time manual upgrade.

The catalog-facing desktop entry has `Name=CyberSnapper`, its installed icon, and `X-AppImage-Version`. The AppStream source is installed as `net.cyberbrand.CyberSnapper.appdata.xml`, a filename understood by the upstream worker; the schema remains compatible with Ubuntu 22.04.

The release rehearsal downloads the just-built x64 artifact and serves those exact bytes to the unmodified, pinned catalog worker. It preserves startup/catalog screenshots, logs, and candidate hashes separately from release assets. This test does not submit or imply acceptance into the catalog.

After the public release and downloaded update sidecars pass verification, submit one file to [AppImage/appimage.github.io](https://github.com/AppImage/appimage.github.io):

- Path: `data/CyberSnapper`
- Sole line: `https://github.com/Alex9001/CyberSnapper`

Confirm no entry or open duplicate submission exists first. The upstream PR must pass its own checks and maintainer review before calling CyberSnapper listed.
