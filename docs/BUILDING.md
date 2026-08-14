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

The release workflow in `.github/workflows/release.yml` performs builds for Linux x64, Windows x64, macOS x64, and macOS arm64. Its packages are intentionally unsigned/ad-hoc signed until production signing identities are configured.
