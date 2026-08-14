# CyberSnapper 2

CyberSnapper is a native Qt desktop application for repeatable website captures on macOS, Windows, and Linux. A small background agent owns jobs, schedules, projects, and the optional localhost API; a private Playwright worker performs browser automation.

CyberSnapper 2 is a clean break from the original script. It does not read, import, or depend on 1.x configuration or state.

## What is included

- Native Qt Widgets application with Capture, History, Compare, Schedules, and Settings workflows.
- Background tray/headless agent with a persistent FIFO job queue and crash recovery.
- Native `cybersnapper` CLI using the same jobs and project history as the GUI.
- Portable folder projects backed by SQLite, with captures, baselines, diffs, reports, and event history.
- Chromium, Firefox, and WebKit support through Playwright; Chromium is bundled in release packages and other engines install on demand.
- Full-page, viewport, and CSS-element capture modes.
- PNG, WebP, AVIF, and Chromium PDF output.
- Custom viewports, profiles, delays, selectors, popup blocking, network blocklists, output naming, safe collision handling, and bounded concurrency.
- Visual baselines, image diffs, mismatch thresholds, and comparison history.
- Once, interval, daily, weekly, and monthly schedules with IANA time zones.
- Authenticated REST v1 API and resumable server-sent job events, bound only to `127.0.0.1`.

CyberSnapper intentionally accepts explicit public HTTP(S) URLs only. The capture worker rejects local/private destinations, embedded URL credentials, and private redirect/resource targets.

## Build from source

Requirements: CMake 3.24+, a C++20 compiler, Qt 6.8+ (`Core`, `Gui`, `Widgets`, `Network`, `Sql`, `Svg`, and `Test`), Node.js 20+, and npm. `cpp-httplib` is discovered from the system or fetched by CMake.

```bash
npm install
npm run typecheck:worker
npm run build:worker

cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

The development binaries are under `build/native/native/`:

```bash
build/native/native/CyberSnapper
build/native/native/cybersnapper agent status
build/native/native/cybersnapper capture https://example.com
```

Install browser engines into CyberSnapper's managed browser cache from Settings, or use Playwright for a source checkout:

```bash
npx playwright install chromium
```

See [Building and packaging](docs/BUILDING.md) for platform and release details.

## CLI examples

```bash
# Capture and wait for completion
cybersnapper capture https://example.com https://example.org

# Batch URLs, multiple engines/formats, JSON result
cybersnapper capture --file urls.txt --engine chromium --engine firefox \
  --format png --format webp --json

cybersnapper jobs --limit 25
cybersnapper job show JOB_ID --json
cybersnapper job cancel JOB_ID
cybersnapper job retry JOB_ID
cybersnapper projects list
cybersnapper projects open /path/to/project
cybersnapper schedules list
cybersnapper schedules run SCHEDULE_ID
cybersnapper api enable
cybersnapper api token
cybersnapper agent stop
```

Press Ctrl+C while waiting for a CLI capture to request cancellation. Add `--no-wait` to return as soon as a job is queued.

## Projects

The default “Quick Captures” project lives in the user's Pictures directory. Projects are normal folders and can be moved or archived as one unit. Open `project.cybersnapper.json` to identify a project; internal state is in `.cybersnapper/project.sqlite`.

See [Project format](docs/PROJECT_FORMAT.md) for the on-disk contract.

## REST API

The API is disabled by default. Enable it in Settings or with `cybersnapper api enable`. The generated bearer token is shown once; only its SHA-256 digest is stored.

```bash
curl -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"urls":["https://example.com"],"profileId":"default"}' \
  http://127.0.0.1:39071/api/v1/jobs
```

See [REST API v1](docs/API.md) for routes and event semantics.

## Architecture

```text
Qt GUI ─┐
CLI ────┼─ local framed JSON RPC ─ Agent ─ SQLite project
Tray ───┘                         │  └──── scheduler / REST v1
                                  └─ NDJSON ─ Playwright worker ─ browser
```

The GUI never embeds a browser-based UI. The Node process is an implementation detail of capture jobs and is not a public server. See [Architecture](docs/ARCHITECTURE.md) for component and security boundaries.

## License

CyberSnapper is distributed under the repository's [license](LICENSE). Qt and the bundled runtime dependencies retain their respective licenses.
