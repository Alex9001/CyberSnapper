# Release process

CyberSnapper 2 uses the native cross-platform workflow at `.github/workflows/release.yml`.

1. Run all local checks documented in `docs/BUILDING.md`.
2. Confirm a capture succeeds through the native CLI and appears in the native History view.
3. Update the version in the top-level `CMakeLists.txt`, `package.json`, and the changelog below.
4. Tag the commit (for example, `git tag v2.1.0`) and push the tag.
5. Publish a GitHub release for the tag or manually dispatch **Native Release**.
6. Download and smoke-test every archive before announcing it.

The workflow creates portable archives for Linux x64, Windows x64, macOS x64, and macOS arm64, plus SHA-256 checksums and GitHub/Sigstore provenance attestations. macOS uses ad-hoc signing; Windows and Linux packages are unsigned. No paid signing identity is required.

## v2.1.0

- Reorganized native UI with live capture planning, persisted splitters/tables, searchable history, browser readiness cards, and first-run guidance.
- Full tabbed profile manager, dirty-state Save/Revert flow, complete schedule editor, and login-start integration.
- Compare workspace with side-by-side, overlay, generated-diff, and baseline-manager views.
- Strict project creation/opening, global FIFO queue recovery, baseline-aware scheduled/retry submission, and transactional job events.
- Worker protocol v2 heartbeats, startup/hang handling, serialized filename allocation, disk/artifact/pixel limits, and versioned browser caches.
- DNS-pinning filtering proxy with private-network blocking and explicit per-project localhost access.
- Expanded native/worker/GUI tests, package checksums, and GitHub build-provenance attestations.

## v2.0.0

- Native Qt Widgets GUI, background tray/headless agent, and native CLI.
- Portable SQLite projects with job, event, artifact, baseline, comparison, and schedule history.
- Private typed Playwright worker supporting Chromium, Firefox, WebKit, PNG, WebP, AVIF, PDF, full-page, viewport, and element modes.
- Visual regression baselines and diff generation.
- Time-zone-aware schedules with missed-run coalescing.
- Authenticated loopback REST v1 API with resumable SSE events.
- Managed browser installation and Chromium-ready release layout.
- Cross-platform CI, tests, install layout, and packaging workflow.
