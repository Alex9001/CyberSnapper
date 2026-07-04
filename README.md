<p align="center">
  <h1 align="center">⌁ CyberSnapper</h1>
  <p align="center">
    Full-page screenshots at multiple viewports — CLI <em>and</em> Web UI
    <br>
    Powered by Playwright
  </p>
  <p align="center">
    <a href="#features">Features</a>
    •
    <a href="#quick-start">Quick Start</a>
    •
    <a href="#cli-usage">CLI Usage</a>
    •
    <a href="#web-ui">Web UI</a>
    •
    <a href="#configuration">Configuration</a>
    •
    <a href="#build">Build</a>
  </p>
</p>

## Features

`📸 Full-page` &nbsp; `🎯 Desktop·Tablet·Mobile` &nbsp; `🔄 Lazy-load scroll` &nbsp; `🌐 Web UI` &nbsp; `🖥️ CLI` &nbsp; `🎨 Dark/Light theme` &nbsp; `⚡ Live progress` &nbsp; `🏷️ Custom naming` &nbsp; `📁 One-click open folder` &nbsp; `📦 Standalone binary`

Snap full-page screenshots of websites at **Desktop** (1920×1080), **Tablet** (768×1024), and **Mobile** (375×812) — with automatic lazy-load scrolling, then preview thumbnails instantly in the browser.

---

## Quick Start

### Standalone binary (if built)

```bash
./dist/CyberSnapper          # opens web UI in browser
./dist/CyberSnapper urls.txt # CLI mode
```

### From source — double-click

| Platform | File |
|----------|------|
| Windows  | `run.bat` |
| Linux / macOS | `run.sh` |

Auto-installs dependencies on first run, then opens the web UI.

### From source — terminal

```bash
node capture.js                          # uses urls/urls.txt
node capture.js urls.txt                 # URLs from a file
node capture.js https://example.com ...  # inline URLs
```

---

## CLI Usage

```
node capture.js [urls.txt | url1 url2 ...]
```

The CLI processes all URLs sequentially and prints progress to the terminal:

```
============================================================
[1/2] https://example.com
============================================================

  Desktop ... saved
  Tablet  ... saved
  Mobile  ... saved

============================================================
[2/2] https://google.com
============================================================

  Desktop ... saved
  Tablet  ... saved
  Mobile  ... saved

Done! All screenshots saved to the "screenshots" folder.
```

---

## Web UI

When launched **without arguments**, CyberSnapper starts a local web server and opens your browser.

1. Paste URLs (one per line)
2. Select viewport presets
3. Hit **📸 Snap!**
4. Watch live progress — thumbnails appear as each viewport is captured

| Feature | Description |
|---------|-------------|
| 🎨 **Theme** | Auto-detects system preference (dark/light), toggle in header |
| 📐 **Presets** | Add, remove, or toggle viewport sizes on the fly |
| 🏷️ **Naming** | Custom output filenames with variables (`{hostname}`, `{preset}`, `{width}`, `{height}`, `{domain}`, `{date}`, `{time}`, `{index}`) |
| 🖼️ **Gallery** | Thumbnail previews of every screenshot, click to open |
| 📁 **Open folder** | Reveals screenshots in your file manager |

---

## Configuration

Edit `config.json` to customize presets and naming:

```json
{
  "presets": [
    { "name": "Desktop", "width": 1920, "height": 1080 },
    { "name": "Tablet",  "width": 768,  "height": 1024 },
    { "name": "Mobile",  "width": 375,  "height": 812 }
  ],
  "naming": {
    "template": "{hostname}-{preset}"
  }
}
```

Changes are saved automatically from the Web UI.

### Naming variables

| Variable | Example |
|----------|---------|
| `{hostname}` | `example_com` |
| `{preset}` | `desktop` |
| `{width}` | `1920` |
| `{height}` | `1080` |
| `{domain}` | `example.com` |
| `{date}` | `2026-07-04` |
| `{time}` | `14-30-00` |
| `{index}` | `01` |

---

## Build

```bash
npm run build
```

Produces `dist/CyberSnapper` (~90 MB, includes Playwright). The binary works on the platform it was built on.

---

## Project Structure

```
CyberSnapper/
├── capture.js           CLI entry point
├── run.sh / run.bat     Clickable launchers (web UI)
├── config.json          Viewport presets & naming config
├── src/
│   ├── index.js         Binary entry (CLI or server)
│   ├── capture.js       Core screenshot engine
│   ├── server.js        Web server + UI
│   ├── cli.js           CLI output logic
│   ├── config.js        Config loader/saver
│   └── naming.js        Filename template engine
├── ui/
│   └── index.html       Standalone HTML UI (embedded in server)
├── urls/
│   ├── urls.txt         Default URL list
│   └── example.txt      Example list
├── screenshots/         Output directory (gitignored)
└── dist/                Built binaries (gitignored)
```

---

## Requirements

- **Node.js** 18+ (when running from source)
- **npm** (for `npm install` / `npm run build`)
- The standalone binary has no dependencies beyond the bundled `node_modules/`.

---

## License

ISC
