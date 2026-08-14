# Release process

CyberSnapper 2 uses the native cross-platform workflow at `.github/workflows/release.yml`.

1. Run all local checks documented in `docs/BUILDING.md`.
2. Confirm a capture succeeds through the native CLI and appears in the native History view.
3. Update the version in the top-level `CMakeLists.txt` and the changelog below.
4. Tag the commit (`git tag v2.0.0`) and push the tag.
5. Publish a GitHub release for the tag or manually dispatch **Native Release**.
6. Download and smoke-test every archive before announcing it.

The workflow creates portable archives for Linux x64, Windows x64, macOS x64, and macOS arm64. macOS uses ad-hoc signing; Windows and Linux packages are unsigned. Configure organization signing/notarization separately before presenting packages as trusted production installers.

## v2.0.0

- Native Qt Widgets GUI, background tray/headless agent, and native CLI.
- Portable SQLite projects with job, event, artifact, baseline, comparison, and schedule history.
- Private typed Playwright worker supporting Chromium, Firefox, WebKit, PNG, WebP, AVIF, PDF, full-page, viewport, and element modes.
- Visual regression baselines and diff generation.
- Time-zone-aware schedules with missed-run coalescing.
- Authenticated loopback REST v1 API with resumable SSE events.
- Managed browser installation and Chromium-ready release layout.
- Cross-platform CI, tests, install layout, and packaging workflow.
