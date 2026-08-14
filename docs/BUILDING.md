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

The repository can seed a deterministic project, start an isolated agent, and capture the real Qt interface without live URLs or customer data. Build the native targets first, then run:

```bash
npm run screenshots:docs
npm run build:site
```

The screenshot source files are written to `docs/images/`. The Pages build is staged and validated under `build/pages/`; all temporary configuration, database, socket, and preview state stays under `build/test-runtime/`.

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

CPack is configured for TGZ on Linux, ZIP/NSIS on Windows, and ZIP/DragNDrop on macOS. Platform deployment tools must run before creating a truly portable package:

- Linux: deploy Qt libraries/plugins and preserve executable rpaths.
- Windows: run `windeployqt` for the GUI and agent.
- macOS: run `macdeployqt`, then sign/notarize the complete app bundle for production.

The release workflow in `.github/workflows/release.yml` performs builds for Linux x64, Windows x64, macOS x64, and macOS arm64. It publishes SHA-256 checksums and GitHub/Sigstore build-provenance attestations. macOS packages use ad-hoc signing and Windows packages remain unsigned because the project does not require paid signing credentials.

GitHub documents the Sigstore-backed attestation model at <https://docs.github.com/en/actions/concepts/security/artifact-attestations>. Apple ties Developer ID distribution/notarization to its paid developer program (<https://developer.apple.com/support/developer-id/>). If CyberSnapper later adopts MSIX/Store distribution, Microsoft documents Store-managed signing as a no-certificate-cost path at <https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options>.
