# Release process

CyberSnapper 2 uses the native cross-platform workflow at `.github/workflows/release.yml`.

The workflow creates portable archives for Linux x64, Windows x64, macOS x64, and macOS arm64, plus SHA-256 checksums and GitHub/Sigstore provenance attestations. macOS uses ad-hoc signing; Windows and Linux packages are unsigned. No paid signing identity is required. CyberSnapper 2 is a clean break from 1.x; do not add migration steps to a 2.x release.

## 1. Prepare the release

1. Update the version in the top-level `CMakeLists.txt` and `package.json`; both values must match the intended `vMAJOR.MINOR.PATCH` tag.
2. Update the matching file in `docs/releases/`, the changelog below, and the README download/status copy.
3. Run every local check documented in `docs/BUILDING.md`.
4. Confirm a real capture succeeds through the native CLI, appears in History, and produces its expected original and portfolio-styled files in the GUI.
5. Push the candidate commit to `master` and require the Linux, Windows, macOS, and Pages checks to pass.

## 2. Rehearse packaging

1. From GitHub Actions, run **Native Release** on the final candidate commit with `release_tag` left blank.
2. Wait for all four package jobs and their packaged-CLI smoke tests to pass. A blank `release_tag` uploads workflow artifacts but does not create or modify a GitHub release.
3. Download every rehearsal artifact. Confirm the four archive names, extract each archive on its target platform, run the packaged CLI with `--version`, and inspect that the application, agent, worker, Node runtime, Qt runtime, and bundled Chromium are present.
4. Fix any problem in a new commit and repeat the full CI and rehearsal sequence. Do not tag a commit that has not passed rehearsal.

## 3. Freeze the final commit and tag

1. Confirm `master` is clean, synchronized with GitHub, and still points to the rehearsed commit.
2. Confirm the package and CMake versions exactly match the release tag.
3. Create an annotated tag on that commit, for example `git tag -a v2.2.0 -m "CyberSnapper 2.2.0"`, and push the tag.
4. Treat a pushed release tag as immutable. Never move or replace a published `v*` tag; ship a new patch version if tagged source needs a code change.

## 4. Publish

1. Create a draft GitHub release for the existing tag and use the matching `docs/releases/` file as its release notes.
2. Publish the release. The `release: published` event starts **Native Release**, which builds from the release tag and attaches all packages, checksums, and attestations.
3. Do not use a nonblank manual `release_tag` for the normal publication path; it is reserved for recovery of an existing release.

## 5. Verify before announcing

1. Require the complete release workflow to pass, including all four native builds and the publish job.
2. Confirm the release contains `CyberSnapper-linux-x64.tar.gz`, both macOS ZIP files, `CyberSnapper-windows-x64.zip`, and `SHA256SUMS.txt`, with provenance attestations visible in GitHub.
3. Download the published assets, verify every checksum, extract each archive on its target platform, and repeat the packaged CLI `--version` smoke test.
4. Confirm GitHub marks the release as latest and that the README and Pages download links resolve to these assets.
5. Announce the release only after the release page, downloads, checksums, and website have all been verified.

## Recovery

- For a transient runner or upload failure, rerun the failed release-workflow jobs against the same tag.
- If valid packages were built but assets were not attached correctly, manually dispatch **Native Release** with the existing `release_tag`; verify the rebuilt assets and checksums again.
- If the tagged source or a packaged application is defective, do not move the tag or silently replace the release. Document the issue and publish a corrected patch release from a new commit and tag.
- Keep an incomplete release unannounced until recovery succeeds. If downloads may be unsafe or misleading, mark the release as a prerelease while preparing the corrective release.

## v2.2.0

- Portfolio presentation scenes with Clean, Aurora, Sunset, Midnight, Graphite, and custom-solid backgrounds.
- Automatic, frameless, rounded-card, light/dark browser, light/dark tablet, and light/dark phone treatments.
- Content-fit, 16:9, 4:3, and square canvases with padding and shadow choices that do not crop or upscale captures.
- Paired untouched originals and polished `-portfolio` output files for full-page, viewport, and element captures.
- Denser, self-documenting native Capture controls for responsive portfolio sets across desktop, tablet, mobile, and custom viewports.
- Persistent named target sets shared by Capture, schedules, retries, CLI, and REST automation.
- Optional Review workspace with durable decisions, notes, batch actions, filters, search, synchronized inspection modes, exact pixel metrics, and immutable baseline evidence.
- Supporting Dashboard, schedule, retry, REST, and CLI workflows, correct local API port reporting, and SQLite schema v4.

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
