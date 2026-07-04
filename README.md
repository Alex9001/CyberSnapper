# ⌁ CyberSnapper

Snap full-page screenshots of websites at Desktop (1920×1080), Tablet (768×1024), and Mobile (375×812) — with automatic lazy-load scrolling. Powered by Playwright.

## Quick Start

**Standalone binary (if built):**
```bash
./dist/CyberSnapper          # opens web UI in browser
./dist/CyberSnapper urls.txt # CLI mode
```

**Or from source (double-click):**
- **Windows** — `run.bat`
- **Linux / macOS** — `run.sh`

Auto-installs dependencies on first run, then opens the web UI.

**Or from terminal (CLI):**
```bash
node capture.js                          # uses urls/urls.txt
node capture.js urls.txt                 # URLs from a file
node capture.js https://example.com ...  # inline URLs
```

## Web UI

When launched without arguments, CyberSnapper starts a web server and opens your browser. Paste URLs (one per line), hit **Snap!**, and watch the progress live — thumbnails appear as each viewport is captured.

- Dark/light theme (auto-detects system preference, toggle button in header)
- "Open folder" button to reveal screenshots in your file manager

## Build Standalone Binary

```bash
npm run build
# Produces: dist/CyberSnapper (~90 MB, includes Playwright)
```

The binary works on the platform it was built on. For distribution, ship it alongside `node_modules/` or run `npm install` in the target directory.

## How It Works

1. Loads each URL at three viewport sizes
2. Scrolls down to trigger lazy-loaded images
3. Waits for network to settle
4. Saves full-page PNGs as `{domain}-{viewport}.png`

## File Structure

```
CyberSnapper/
  capture.js        ← CLI entry point
  run.sh / run.bat  ← clickable launchers (web UI)
  src/
    index.js        ← binary entry (CLI or server)
    capture.js      ← core screenshot engine
    server.js       ← web server + UI
    cli.js          ← CLI output logic
  urls/
    urls.txt        ← default URL list
    example.txt     ← example list
  screenshots/      ← output (gitignored)
  dist/             ← built binaries (gitignored)
```

## Cross-platform

Works on Windows, macOS, and Linux. The standalone binary must be built separately for each platform.
