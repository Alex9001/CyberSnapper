<div align="center">
  <img src="assets/logo.png" width="190" alt="CyberSnapper camera logo">
  <h1>CyberSnapper</h1>
  <p><strong>Capture every viewport. Catch every change.</strong></p>
  <p>Native website capture and visual regression for macOS, Windows, and Linux.</p>

  <p>
    <a href="https://github.com/Alex9001/CyberSnapper/actions/workflows/ci.yml"><img alt="Build status" src="https://img.shields.io/github/actions/workflow/status/Alex9001/CyberSnapper/ci.yml?branch=master&style=for-the-badge&logo=githubactions&logoColor=white&label=Build"></a>
    <a href="https://github.com/Alex9001/CyberSnapper/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/Alex9001/CyberSnapper?style=for-the-badge&color=19bce8"></a>
    <a href="LICENSE"><img alt="ISC license" src="https://img.shields.io/badge/license-ISC-8b65ff?style=for-the-badge"></a>
  </p>

  <p>
    <a href="https://alex9001.github.io/CyberSnapper/"><strong>Explore the website</strong></a>
    ·
    <a href="https://github.com/Alex9001/CyberSnapper/releases/latest"><strong>Download CyberSnapper</strong></a>
    ·
    <a href="docs/BUILDING.md"><strong>Build from source</strong></a>
  </p>
</div>

![CyberSnapper Dashboard showing review work, failed runs, active jobs, schedules, and recent captures](docs/images/app-dashboard.png)

CyberSnapper is a local-first visual QA studio. Define a capture once, reuse it from the native Qt interface, CLI, scheduler, or authenticated localhost API, and keep every artifact in a portable project folder you control.

It is not a web UI wrapped in a desktop shell. The interface is native Qt Widgets; a private Playwright worker exists only to drive browser captures.

## Download

CyberSnapper 2.2 is the current development line. The latest published 2.1 packages bundle the application, Qt runtime, Node runtime, capture worker, and Chromium. Firefox and WebKit can be installed on demand from Settings.

| Platform | Package |
| --- | --- |
| macOS · Apple silicon | [CyberSnapper-macos-arm64.zip](https://github.com/Alex9001/CyberSnapper/releases/download/v2.1.0/CyberSnapper-macos-arm64.zip) |
| macOS · Intel | [CyberSnapper-macos-x64.zip](https://github.com/Alex9001/CyberSnapper/releases/download/v2.1.0/CyberSnapper-macos-x64.zip) |
| Windows · x64 | [CyberSnapper-windows-x64.zip](https://github.com/Alex9001/CyberSnapper/releases/download/v2.1.0/CyberSnapper-windows-x64.zip) |
| Linux · x64 | [CyberSnapper-linux-x64.tar.gz](https://github.com/Alex9001/CyberSnapper/releases/download/v2.1.0/CyberSnapper-linux-x64.tar.gz) |

Every release includes SHA-256 checksums and GitHub build-provenance attestations. macOS archives are ad-hoc signed and Windows archives are currently unsigned; see the [packaging notes](docs/BUILDING.md#packages).

## Why CyberSnapper

- Capture desktop, tablet, mobile, and custom viewports with explicit pixel ratio and mobile-mode controls.
- Organize reusable sites into named target sets, then use the same frozen target snapshot from Capture, schedules, retries, the CLI, or the API.
- Run Chromium, Firefox, and WebKit across full-page, viewport, or CSS-element captures.
- Export PNG, WebP, AVIF, and Chromium PDF with collision-safe output naming.
- Triage a persistent review queue, accept a result as the new baseline, ignore expected change, or return it to unreviewed.
- Inspect baseline and current captures side by side with synchronized pan/zoom, overlay, wipe, and generated-difference views.
- Control pixel sensitivity, allowed mismatch, and dynamic-element exclusion per profile.
- Schedule once, interval, daily, weekly, or monthly runs in real IANA time zones.
- Share one durable job queue and project history across the GUI, CLI, scheduler, and REST API.
- Move or archive a project as one normal folder containing its manifest, captures, baselines, diffs, and SQLite state.

## A complete visual workflow

| Dashboard | Capture |
| --- | --- |
| See review work, failed or partial runs, active jobs, the next schedule, and recent activity at a glance. | Combine one-time URLs or a saved target set with explicit viewports, browsers, formats, and comparison rules. |
| ![CyberSnapper Dashboard](docs/images/app-dashboard.png) | ![CyberSnapper Capture workspace](docs/images/app-capture.png) |

| Targets | Review |
| --- | --- |
| Maintain labeled, reusable site collections with paste, TXT/CSV import and export, ordering, and per-target enable controls. | Work through detected changes with plain-language metrics, keyboard review actions, synchronized inspection, and immutable baseline history. |
| ![CyberSnapper target sets](docs/images/app-targets.png) | ![CyberSnapper visual Review workspace](docs/images/app-review.png) |

History and persistent local-time schedules remain first-class workspaces, backed by the same durable project queue.

The screenshots above are generated automatically from a deterministic demo project. They contain no live or customer data. Regenerate them with `npm run screenshots:docs` after building the native test targets.

## How it fits together

```text
Native Qt GUI ─┐
CLI ───────────┼── private framed JSON RPC ── Agent ── portable SQLite project
REST v1 ───────┘                                │         captures / baselines / diffs
                                               ├── scheduler + durable job queue
                                               └── NDJSON ── Playwright worker ── browsers
```

The API is disabled by default, binds only to `127.0.0.1`, and stores only the SHA-256 digest of its generated bearer token. Navigation policy rejects non-HTTP protocols and embedded credentials, keeps private LAN destinations blocked, makes localhost a per-project opt-in, and resolves/pins outbound destinations so redirects and subresources cannot bypass the policy.

Read the deeper guides:

- [Architecture and security boundaries](docs/ARCHITECTURE.md)
- [Portable project format](docs/PROJECT_FORMAT.md)
- [REST API v1](docs/API.md)
- [Building and packaging](docs/BUILDING.md)

## Build from source

You need CMake 3.24+, a C++20 compiler, Qt 6.8+ (`Core`, `Gui`, `Widgets`, `Network`, `Sql`, `Svg`, and `Test`), Node.js 20+, and npm.

```bash
npm install
npm run typecheck:worker
npm run build:worker
npm run test:worker

cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

Development binaries land in `build/native/native/`:

```bash
build/native/native/CyberSnapper
build/native/native/cybersnapper-cli agent status
build/native/native/cybersnapper-cli capture https://example.com
```

Install Chromium into the development browser cache with `npx playwright install chromium`, or install engines from the application’s Settings page.

## CLI in thirty seconds

```bash
# Capture two sites and wait for completion
cybersnapper-cli capture https://example.com https://example.org

# Batch targets, engines, and formats with a JSON result
cybersnapper-cli capture --file urls.txt \
  --engine chromium --engine firefox \
  --format png --format webp --json

# Reuse a saved target set and triage its results
cybersnapper-cli targets list --json
cybersnapper-cli capture --target-set TARGET_SET_ID --json
cybersnapper-cli review list --json
cybersnapper-cli review accept COMPARISON_ID --revision 0

cybersnapper-cli jobs --limit 25
cybersnapper-cli job show JOB_ID --json
cybersnapper-cli job retry JOB_ID
cybersnapper-cli schedules list
cybersnapper-cli schedules run SCHEDULE_ID
```

Press Ctrl+C while waiting to request cancellation, or add `--no-wait` to return as soon as the job is queued.

## REST API

Enable the local API in Settings or with `cybersnapper-cli api enable`, then keep the one-time token somewhere safe.

```bash
curl -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"urls":["https://example.com"],"profileId":"default"}' \
  http://127.0.0.1:39071/api/v1/jobs
```

The API supports target sets, dashboard summaries, capture submission, job inspection, comparison review, project discovery, and resumable server-sent job events. See [REST API v1](docs/API.md) for the full contract.

## CyberSnapper 1.x archive

CyberSnapper 2 is a clean break from the original script. It does not import, read, or depend on 1.x state, and no migration path is required. The final 1.x source and downloadable archive remain frozen in the [v1.0.0 release](https://github.com/Alex9001/CyberSnapper/releases/tag/v1.0.0) for anyone who still needs them.

## License

CyberSnapper is open source under the [ISC license](LICENSE). Qt, Playwright, Chromium, and other packaged dependencies retain their own licenses.
