# Screenshot Bot

Take full-page screenshots of websites at Desktop (1920×1080), Tablet (768×1024), and Mobile (375×812) resolutions — with automatic lazy-load scrolling.

## Quick Start

**Windows** — double-click `run.bat`

**Linux / macOS** — double-click `run.sh` (or run from terminal):

```bash
./run.sh
```

It will install dependencies automatically and capture every URL in `urls/urls.txt`.

## Usage

```bash
node capture.js <url1> <url2> ...        # inline URLs
node capture.js urls.txt                 # URLs from a file
node capture.js                          # uses urls/urls.txt by default
```

Every URL is captured at all three viewports and saved as PNGs in the `screenshots/` folder.

## Setup (manual)

```bash
npm install
npx playwright install chromium
```

## File Structure

```
screenshot-bot/
  capture.js        ← the main script
  run.sh            ← shortcut (Linux/macOS)
  run.bat           ← shortcut (Windows)
  urls/
    urls.txt        ← default URL list
    example.txt     ← example URL list
  screenshots/      ← output folder (gitignored)
  package.json
```

## How It Works

1. Loads each URL at three viewport sizes
2. Scrolls down the page to trigger lazy-loaded images
3. Waits for the network to settle
4. Saves a full-page screenshot as `{domain}-{viewport}.png`
