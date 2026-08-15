# Building and packaging

## Local developer build

Install Qt 6.8+, CMake 3.24+, a C++20 compiler, Node.js 20+, and npm. Then:

```bash
npm install
npm run typecheck:worker
npm run build:worker
npm run test:worker
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

`cpp-httplib` may be installed system-wide. If it is absent, CMake fetches the pinned v0.52.0 source.

## Documentation screenshots and website

The repository can seed a deterministic project, start an isolated agent, and capture the real Qt interface without contacting live URLs during normal docs or CI builds. It uses committed light and dark captures of the public CYBER BRAND homepage. Build the native targets first, then run:

```bash
npm run screenshots:docs
npm run build:site
```

The screenshot source files are written to `docs/images/`. The Pages build is staged and validated under `build/pages/`; all temporary configuration, database, socket, and preview state stays under `build/test-runtime/`.

To intentionally refresh the six committed CYBER BRAND desktop/tablet/mobile source captures from `https://cyberbrand.net/`, run `npm run screenshots:sources`. That explicit command is the only documentation step that requires network access; it fixes the site theme and pauses motion before capture. Review the resulting light and dark images before regenerating `docs/images/`.

On Linux, the screenshot command uses Qt's offscreen platform automatically. If the host applies a local-socket sandbox, grant the command permission to create its isolated IPC socket while keeping its filesystem state inside the repository.

GitHub Pages deploys `build/pages/` through `.github/workflows/pages.yml`. The site intentionally has no framework or remote runtime dependencies; only its release-download resolver calls the public GitHub Releases API.

## Install tree

```bash
cmake --install build/native --prefix stage
```

The install includes the GUI, tray/headless agent, CLI, worker bundle, worker production dependencies, license, and documentation. If present, `.runtime/` and `.playwright-browsers/` are included as the private Node runtime and initial browser cache.

Build the latter inputs with the same target platform and architecture as the package:

```bash
mkdir -p .runtime .playwright-browsers
cp "$(command -v node)" .runtime/node
PLAYWRIGHT_BROWSERS_PATH="$PWD/.playwright-browsers" npx playwright install chromium
npm prune --omit=dev
```

On Windows, stage `node.exe` as `.runtime/node.exe`.

## Packages

The mechanics of the release pipeline — workflow jobs, platform tooling, the
package smoke test, and publishing — are documented in
[PACKAGING.md](PACKAGING.md).

Release packaging produces an install-friendly package and a portable archive for every supported architecture:

| Platform | Architectures | Primary package | Portable archive |
| --- | --- | --- | --- |
| Linux | x64, arm64 | AppImage | tar.gz |
| Windows | x64, arm64 | NSIS setup executable | ZIP |
| macOS | x64, arm64 | DMG | ZIP |

Platform deployment tools must run before creating these self-contained packages:

- Linux: deploy Qt libraries/plugins and preserve executable rpaths.
- Windows: run `windeployqt` for the GUI and agent.
- macOS: run `macdeployqt`, then ad-hoc sign the complete app bundle. A future Developer ID release would also require Apple notarization.

The release workflow in `.github/workflows/release.yml` performs native builds for Linux x64 and arm64, Windows x64 and arm64, and macOS x64 and arm64. It publishes SHA-256 checksums and GitHub/Sigstore build-provenance attestations for all twelve packages. macOS application bundles use ad-hoc signing but are not notarized; Windows installers and Linux packages remain unsigned because the project does not require paid signing credentials.

GitHub documents the Sigstore-backed attestation model at <https://docs.github.com/en/actions/concepts/security/artifact-attestations>. Apple ties Developer ID distribution/notarization to its paid developer program (<https://developer.apple.com/support/developer-id/>). If CyberSnapper later adopts MSIX/Store distribution, Microsoft documents Store-managed signing as a no-certificate-cost path at <https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options>.
