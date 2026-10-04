# Release process

CyberSnapper 2 uses the native cross-platform workflow at `.github/workflows/release.yml`. How the packaging and publishing pipeline works under the hood is documented in [docs/PACKAGING.md](docs/PACKAGING.md).

The workflow creates AppImage and tar.gz packages for Linux x64 and arm64, setup executables and portable ZIPs for Windows x64 and arm64, and DMGs and ZIPs for macOS x64 and arm64. It also publishes SHA-256 checksums and GitHub/Sigstore provenance attestations. macOS application bundles use ad-hoc signing but are not notarized; Windows and Linux packages are unsigned. No paid signing identity is required. CyberSnapper 2 is a clean break from 1.x; do not add migration steps to a 2.x release.

## 1. Prepare the release

1. Update the version in `CMakeLists.txt`, `package.json`, `package-lock.json`, the desktop `X-AppImage-Version`, AppStream latest release, and website structured metadata. Run `node scripts/check-release-version.mjs vMAJOR.MINOR.PATCH`; every value must match.
2. Update the matching file in `docs/releases/`, the changelog below, and the README download/status copy.
3. Run every local check documented in `docs/BUILDING.md`.
4. Confirm a real capture succeeds through the native CLI, appears in History, and produces its expected original and portfolio-styled files in the GUI.
5. Push the candidate commit to `master` and require the Linux, Windows, macOS, and Pages checks to pass.

## 2. Rehearse packaging

1. From GitHub Actions, run **Native Release** on the final candidate commit with `release_tag` left blank.
2. Wait for all six platform/architecture package jobs, packaged capture smoke tests, Ubuntu 22.04/24.04 runtime checks, the upstream AppImage catalog worker, and complete release assembly to pass. A blank `release_tag` uploads workflow artifacts but does not create or modify a GitHub release.
3. Download every rehearsal artifact. Confirm all twelve package names and both `.AppImage.zsync` sidecars, install or extract each package on its target platform, run the packaged CLI with `--version`, and inspect that the application, agent, worker, Node runtime, Qt runtime, and bundled Chromium are present.
4. Fix any problem in a new commit and repeat the full CI and rehearsal sequence. Do not tag a commit that has not passed rehearsal.

## 3. Freeze the final commit and tag

1. Confirm `master` is clean, synchronized with GitHub, and still points to the rehearsed commit.
2. Confirm the package and CMake versions exactly match the release tag.
3. Create an annotated tag on that commit, for example `git tag -a v2.2.2 -m "CyberSnapper 2.2.2"`, and push the tag.
4. Treat a pushed release tag as immutable. Never move or replace a published `v*` tag; ship a new patch version if tagged source needs a code change.

## 4. Stage verified packages, then publish

1. Dispatch **Native Release** at the exact tag, with that same tag as the input:

   ```bash
   gh workflow run release.yml --ref vMAJOR.MINOR.PATCH -f release_tag=vMAJOR.MINOR.PATCH
   ```

   All candidate checkouts use the dispatch's immutable commit SHA. The tag,
   workflow ref, checked-out source, and declared version must agree. Dispatching
   a branch with a different `release_tag` is rejected.
2. Wait for every native package, browser, runtime, catalog, and assembly gate.
   The final job validates all fourteen assets and checksums, creates provenance
   attestations, then creates or resumes a **draft** release using the committed
   notes. It never publishes a release, overwrites an asset, or deletes a file.
3. Require successful staging and independently confirm the draft contains all
   fifteen files (fourteen assets plus `SHA256SUMS.txt`). Every remote file must
   be uploaded and match the checked local size and SHA-256 digest. Verify the
   tag still resolves to the rehearsed commit and provenance identifies it.
4. Do not publish while any staging run is active. Workflow concurrency
   serializes staging runs for a tag, but cannot lock out a maintainer publishing
   from another session. After all checks pass, explicitly publish the complete
   draft:

   ```bash
   gh release edit vMAJOR.MINOR.PATCH --draft=false --latest
   ```

   For prereleases, keep the prerelease flag and do not mark the release latest.
   Publication does **not** trigger a rebuild or replace the verified files.

## 5. Verify before announcing

1. Require the complete release workflow to pass, including all six native builds and the draft-staging job.
2. Confirm the release contains all of the following, plus `SHA256SUMS.txt`, with provenance attestations visible in GitHub:

   - `CyberSnapper-linux-x64.AppImage`, its `.zsync` sidecar, and `CyberSnapper-linux-x64.tar.gz`
   - `CyberSnapper-linux-arm64.AppImage`, its `.zsync` sidecar, and `CyberSnapper-linux-arm64.tar.gz`
   - `CyberSnapper-windows-x64-setup.exe` and `CyberSnapper-windows-x64-portable.zip`
   - `CyberSnapper-windows-arm64-setup.exe` and `CyberSnapper-windows-arm64-portable.zip`
   - `CyberSnapper-macos-x64.dmg` and `CyberSnapper-macos-x64.zip`
   - `CyberSnapper-macos-arm64.dmg` and `CyberSnapper-macos-arm64.zip`

3. Download the published assets, verify every checksum, install or extract each package on its target platform, and repeat the packaged CLI `--version` smoke test.
4. Run `python3 scripts/check-appimage-update.py <downloaded-AppImage>` on each native architecture. Confirm the embedded channel, sidecar content, and reconstruction, not just asset presence.
5. Confirm GitHub marks the release as latest and that the README and Pages download links resolve to these assets.
6. Announce the release only after the release page, downloads, checksums, and website have all been verified.

## Recovery

- A failed build leaves the public release unchanged. Fix any source defect in a
  new candidate commit and repeat CI and the full rehearsal before tagging.
- For a transient draft-upload failure, rerun only the failed assembly/staging
  job of the same workflow run. Its retained platform artifacts preserve the
  exact bytes. Matching existing draft assets are retained and missing files
  are uploaded; a full rebuild may produce different package bytes and is not
  a substitute for resuming those artifacts.
- A conflicting, incomplete (`starter`), zero-byte, duplicate, unexpected, or
  digest-less remote asset blocks staging. Nothing is deleted automatically.
  Inspect the draft and obtain explicit approval for any destructive cleanup;
  never publish a partial draft to work around a failure.
- Published releases are read-only to this workflow. If tagged source or a
  published package is defective, document the issue and ship a corrected patch
  version from a new commit and tag. Do not move a published `v*` tag or silently
  overwrite its assets.
- Keep workflow artifacts and validation evidence until public download and
  provenance verification are complete. Never announce a release before that.

## v2.4.2

- **External AppImage updates**: Stable architecture-specific channels and validated zsync sidecars for x64 and arm64; existing download filenames remain unchanged.
- **Catalog metadata**: Versioned desktop/AppStream metadata and a compatible installed appdata filename.
- **Release gates**: Ubuntu 22.04/24.04 x64 ELF/startup audit, exact-candidate upstream catalog worker, and complete fourteen-asset checksum validation.
- **Dependency updates**: Separate security/runtime/development groups, individual major updates, strict engine and minimum-Node coverage, and required browser/native CI checks.

## v2.4.1

- **Compact Capture workspace**: Smaller buttons, tighter spacing, contextual comparison settings, and scrollable options with capture actions and progress always reachable.
- **Clean screenshot edges**: Remove reserved root scrollbar gutters before capture, without cropping or resizing the output.
- **No redundant theme files**: Both checks exact rendered PNG bytes and omits identical dark files and portfolio copies. Each theme still loads separately; PDF jobs and existing dark comparison baselines retain both outputs.
- **Accurate progress**: Adjust planned output counts when duplicate theme files are omitted and report omissions on completion.
- **Regression coverage**: Short-window GUI layout, gutter-free full-page images, theme deduplication, concurrent target isolation, dark-only captures, and baseline preservation.

## v2.4.0

- **Website themes**: Capture Light, Dark, or Both using the browser's preferred color scheme before navigation; paired captures retain separate files and comparison baselines.
- **Descriptive filenames**: The default includes the hostname, page path, and viewport, such as `example.com-sample-Desktop.png`; paired themes receive distinct suffixes.
- **Live capture progress**: Follow capture position, URL, viewport, browser, theme, preparation stage, elapsed time, and processed-file counts, including parallel tasks.
- **Output access**: Open the active project's captures directory directly from a prominent button on Capture.
- **Regression coverage**: Real browser theme rendering, custom filename collisions, progress, skipped outputs, failures, cancellation, profile normalization, and theme-aware job limits.

## v2.3.3

- **Chromium stability**: Abruptly closed HTTPS proxy tunnels no longer raise an unhandled `EPIPE` that terminates the capture worker.
- **Accurate browser results**: Browser installation and verification completion events are consumed from either process channel, terminal output is drained before exit, and protocol JSON never leaks into the interface.
- **Honest terminal states**: Successful engines finish as installed and ready; failed installs stop showing an active installation state and retain diagnostics under Details.
- **Safe upgrades**: A newer GUI or CLI replaces an idle older agent automatically. Busy older agents are left running and reported as incompatible instead of being interrupted or used silently.
- **Regression coverage**: Adds dropped-tunnel, stderr completion, failed-install, and real three-viewport Chromium capture verification.

## v2.3.2

- **Working managed installs**: Fixes Playwright CLI resolution in packaged applications, including AppImages, so Firefox, WebKit, and Chromium repair operations can actually run.
- **Visible progress**: Settings now shows one-at-a-time download queues, component progress, elapsed time, verification, cancellation, repair, and expandable installer output.
- **Honest readiness**: Downloaded engines must launch successfully before capture is enabled; missing host libraries remain installed and are reported with exact diagnostics and safe, reviewable guidance.
- **CLI controls**: Adds `browsers status`, `install`, `verify`, and `cancel` commands for local automation.
- **Packaging confidence**: The release smoke test now exercises the packaged installer before a real capture, and detached agent startup no longer leaves automation callers waiting on inherited output handles.

## v2.3.1

- **Correct packages**: Restores the complete Linux, Windows, and macOS x64/arm64 release matrix with immutable, checksum-verified Linux packaging tools.
- **Safer blocking**: Rejected filters can no longer reach the rules engine, consent handling covers frames, and delayed banner removal releases scroll locks correctly.
- **Working custom rulesets**: Profile selections now survive the configuration dialog and newly created rulesets can be enabled immediately.
- **Reliable subscriptions**: Redirects use their validated destination, oversized downloads abort early, and identical rules produce one stable content-addressed snapshot.
- **Release integrity**: Fixes malformed workflow YAML, native source warnings, version metadata, browser-matrix enforcement, and inaccurate packaging documentation.

## v2.3.0

- **Content blocking**: Cookie banner removal, consent handling, site exceptions, custom rulesets, and visual review.
- **Security**: Capture boundary, HTTPS-only downloads, atomic writes, and last-known-good snapshots.
- **Compatibility**: Browser matrix (Chromium/Firefox/WebKit) and schema v5.
- **Developer**: REST API v1 (`/v1/contentBlocking/*`), RPC v1 (`contentBlocking.*`), and worker protocol v2.
- **Documentation**: Updated `ARCHITECTURE.md`, `PROJECT_FORMAT.md`, and `THIRD-PARTY.md`.
- **Packaging**: AppImage/tar.gz (Linux x64/arm64), NSIS/ZIP (Windows x64), DMG/ZIP (macOS x64/arm64).

## v2.2.2

- The Chromium engine falls back to Google Chrome and then Microsoft Edge when the bundled Chromium is missing.
- On Windows arm64, an x64 (emulated) bundled Chromium is replaced by a native system Chrome or Edge when available.
- The fallback applies automatically on Windows, Linux, and macOS.

## v2.2.1

- Native package coverage for x64 and arm64 on Linux, Windows, and macOS.
- AppImages as the recommended Linux download, with portable tar.gz archives retained for manual installation.
- Windows setup executables as the recommended download, with portable ZIP archives available for no-install use.
- macOS DMGs as the recommended download, with ZIP archives available as a portable alternative.
- Package smoke tests, SHA-256 checksums, and GitHub/Sigstore provenance attestations across the full release matrix.

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
