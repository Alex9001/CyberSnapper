# Release

## How to create a GitHub release

### 1. Tag the release

```bash
# Make sure you're on master with all changes committed and pushed
git checkout master
git pull

# Tag with the next version number
git tag v0.1.0

# Push the tag to GitHub
git push origin v0.1.0
```

> This triggers the **Build & Release** workflow in GitHub Actions.
> It builds all 4 platform binaries and waits for you to publish.

### 2. Create the release on GitHub

Go to your repo on GitHub → **Releases** → **Create a new release** (or **Draft a new release**).

Set these fields:

| Field | Value |
|-------|-------|
| **Tag** | `v0.1.0` (select the tag you just pushed) |
| **Release title** | `v0.1.0` |

### 3. Write the release notes

Copy the template below into the description box and fill in the highlights.

## Release notes template

Copy this into the GitHub release description box and update it for your release:

```markdown
## What's new

<!-- Delete sections that don't apply, add details where marked -->

### ✨ Features
- Full-page screenshots with Playwright (Chrome)
- PNG and PDF output formats in the standalone binary
- WebP and AVIF when running from source (not available in the binary)
- Desktop / Tablet / Mobile viewport presets (configurable)
- Concurrent URL processing with configurable worker pool
- Popup / cookie banner blocking (built-in blocklist + user blocklist)
- Element hide / wait-for-selector before capture
- Custom filename templates (`{hostname}`, `{preset}`, `{width}`, etc.)
- Web UI with live SSE progress stream, viewport thumbnails, and lightbox
- REST API endpoint (`/api/screenshot`) for automation
- CLI mode for batch processing from terminal
- Standalone binary — cross-compiled for Linux, Windows, macOS (Intel & Apple Silicon)
- Auto-install of Chromium and system dependencies on first run
- Auto-stop after 15 minutes idle, single-instance lock

### 🎨 UI / UX
- Cyberpunk dark theme with warm muted light theme option
- Live status bar during Chromium install (spinner + progress bar)
- Framed viewport cards with loading animations, grouped under URLs
- Lightbox viewer for full-resolution screenshots
- Theme toggle persists choice to `config.json`
- Help popup documenting features

### 🐛 Bug Fixes
- Header whitespace stripped from full-page PNGs
- Popup/modal blocking via combined blocklist + hide-popup CSS injection
- Server crash on Chromium install failure → graceful error in UI

### ⚙️ Internal / Dev
- Modular architecture: `src/capture/`, `src/server/`, `src/cli.js`, etc.
- Unit tests for browser install
- Binary build script cross-compiles for 4 platform targets
- CI workflow builds and attaches binaries on release
- Windows launcher with auto Node.js install via winget
- Linux/macOS launcher with package-manager-specific install guidance

---

**Full changelog**: [`v0.0.0..v0.1.0`](https://github.com/Alex9001/CyberSnapper/compare/v0.0.0...v0.1.0)
```

> **Tip:** The "Full changelog" link won't work for the very first release — just delete that line or change `v0.0.0` to the previous tag once you have one.

Before releasing, update `RELEASE.md`'s Changelog section with anything new added since the last release. Then tag, publish, and copy-paste the notes.

### 4. Publish

Click **Publish release**. The workflow attaches the 4 binaries automatically.

---

## Changelog

### v0.1.0 (current)

Current state — no release tag yet. This is the first release placeholder.

> Before tagging the first release, copy the content below into the release
> notes template above. Delete what you don't need and add anything missing.
> After releasing, update the version here for the next release.

#### ✨ Features
- Full-page screenshots with Playwright (Chrome)
- PNG and PDF output formats in the standalone binary
- WebP and AVIF when running from source (not available in the binary)
- Desktop / Tablet / Mobile viewport presets (configurable)
- Concurrent URL processing with configurable worker pool
- Popup / cookie banner blocking (built-in blocklist + user blocklist)
- Element hide / wait-for-selector before capture
- Custom filename templates (`{hostname}`, `{preset}`, `{width}`, etc.)
- Web UI with live SSE progress stream, viewport thumbnails, and lightbox
- REST API endpoint (`/api/screenshot`) for automation
- CLI mode for batch processing from terminal
- Standalone binary (`dist/`) via `@yao-pkg/pkg` — cross-compiled for Linux, Windows, macOS (Intel & Apple Silicon)
- Auto-install of Chromium and system dependencies on first run
- Auto-stop after 15 minutes idle, single-instance lock

#### 🎨 UI / UX
- Cyberpunk dark theme with warm muted light theme option
- Live status bar during Chromium install (spinner + progress bar)
- Framed viewport cards with loading animations, grouped under URLs
- Lightbox viewer for full-resolution screenshots
- Theme toggle persists choice to `config.json`
- Help popup documenting features

#### 🐛 Bug Fixes
- Header whitespace stripped from full-page PNGs (via sharp)
- Popup/modal blocking via combined blocklist + hide-popup CSS injection
- Server crash on Chromium install failure → graceful error in UI

#### ⚙️ Internal / Dev
- Modular architecture: `src/capture/`, `src/server/`, `src/cli.js`, etc.
- Unit tests for browser install (`test/browser.test.js`)
- Binary build script (`build.js`) cross-compiles for 4 platform targets
- CI workflow (`.github/workflows/release.yml`) builds and attaches binaries on release
- Windows launcher (`run.bat`) with auto Node.js install via winget
- Linux/macOS launcher (`run.sh`) with package-manager-specific install guidance

