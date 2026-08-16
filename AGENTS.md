# AGENTS.md

## Commits

Do not add AI-tool attribution trailers to commit messages in this
repository — not "Generated with Codebuff", not "Co-Authored-By: Codebuff",
and not the equivalent footers from other AI tools (Copilot, Claude, Codex,
ChatGPT, Gemini, Cursor, etc.). Commit messages should consist of the subject
line, an optional body, and nothing else. Human "Co-Authored-By: Person"
trailers are fine.

## What CyberSnapper is

A local-first, cross-platform website screenshot tool and portfolio mockup
generator. The interface is native Qt Widgets; a private Playwright worker
does the rendering and capturing. Everything runs locally — no accounts,
uploads, or template services.

## Architecture

Four processes, each with a narrow job:

| Process | Responsibility | Persistent state |
|---|---|---|
| `CyberSnapper` (GUI) | Native Qt Widgets UI | Window/UI preferences only |
| `cybersnapper-agent` | Single project owner, job queue, schedules, REST API, tray | QSettings and project SQLite databases |
| `cybersnapper-cli` | Native automation CLI | None; talks to the agent |
| `worker/dist/main.cjs` | One capture job using Playwright and Sharp | Artifacts inside the selected project |

- GUI/CLI ↔ agent: versioned, length-prefixed JSON frames over `QLocalSocket`
  (protocol v1, `Rpc.h`/`Rpc.cpp`, frames capped at 16 MiB). The OS user is
  the IPC security boundary.
- Agent ↔ worker: one worker process per active job, protocol-v2
  newline-delimited JSON. The agent assigns final event sequence numbers,
  applies state changes transactionally, then broadcasts events.
- Projects are portable SQLite databases (WAL mode, foreign keys, per-project
  lock file). Only the agent opens them for normal operation.
- REST v1 is disabled by default, binds to `127.0.0.1` only, and requires a
  high-entropy bearer token (only the SHA-256 digest is stored).

See `docs/ARCHITECTURE.md` for the full security model (capture boundary,
visual review model, scheduling) and `docs/PROJECT_FORMAT.md` for the project
format.

## Repository layout

- `native/` — C++20/Qt 6.8+ application: `src/core` (shared library),
  `src/gui`, `src/agent`, `src/cli`, plus `tests/` (CMake + ctest).
- `worker/` — TypeScript Playwright worker (`src/`), bundled by esbuild to
  `dist/*.cjs`, with `test/*.test.cjs` run via `node --test`.
- `scripts/` — release, packaging, docs-screenshot, and site tooling.
- `docs/` — architecture, project format, API, building, packaging, release
  notes (`docs/releases/`).
- `site/` — static marketing site, built with `scripts/build-site.mjs`, no
  framework or remote runtime dependencies.

## Native (C++/Qt) conventions

- Follow the existing style: `namespace CyberSnapper` with nested namespaces
  (e.g. `CyberSnapper::Paths`), members prefixed `m_`, file-local constants
  prefixed `k`, and `QStringLiteral` for string literals.
- Data structures live in `src/core/Models.h` as plain structs with
  `toJson`/`fromJson` free functions; keep the JSON contract stable — it is
  shared across GUI, CLI, and worker.
- The worker speaks protocol v2 (`worker/src/protocol.ts`); the native side
  mirrors those shapes in `Models.h`. When you change one side, keep the
  other in sync.
- `cybersnapper_core` links Qt `Core`/`Network`/`Sql`; the GUI adds
  `Widgets`/`Svg`. Register new core sources in `native/CMakeLists.txt`.
- `cpp-httplib` is used for REST; if absent, CMake fetches the pinned
  v0.52.0 source — do not bump it casually.
- Keep `package.json` and `CMakeLists.txt` versions in sync;
  `scripts/check-release-version.mjs` enforces this in CI.

## Worker (TypeScript) conventions

- Strict TypeScript, compiled by esbuild to CommonJS targeting Node 20 with
  `playwright` and `sharp` external.
- `protocol.ts` is the single source of truth for job/artifact/event shapes.
- `network.ts` implements the URL policy: only explicit `http`/`https`,
  no embedded credentials, private/LAN/link-local destinations blocked,
  localhost only when the project opts in. Never weaken these checks — the
  filtering proxy is the capture boundary.
- Tests are plain `.test.cjs` files using `node:test` and
  `node:assert/strict`, exercising internals exported from
  `worker/dist/testing.cjs`. Run `npm run typecheck:worker` and
  `npm run test:worker` after worker changes.

## Building, testing, packaging

```bash
npm install
npm run typecheck:worker
npm run build:worker
npm run test:worker

cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

Development binaries land in `build/native/native/`. Install Chromium for
local capture work with `npx playwright install chromium`.

Packaging is driven by `.github/workflows/release.yml`; each platform
(AppImage/tar.gz, NSIS/ZIP, DMG/ZIP) runs `scripts/smoke-packaged-capture.mjs`
before anything is published. See `docs/PACKAGING.md` for details and
`RELEASE.md` for the release process. Never move a published `v*` tag.

## Cross-cutting rules

- Keep features centered on CyberSnapper's primary job: portfolio-ready
  website screenshots.
- Respect the security boundaries (IPC, capture, REST) documented in
  `docs/ARCHITECTURE.md`; treat them as invariants, not suggestions.
- Keep the docs site deterministic: `npm run screenshots:docs` never touches
  the live network; only the explicit `npm run screenshots:sources` does.
- For UI changes, attach before/after screenshots to the pull request.
