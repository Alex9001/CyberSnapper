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

`📸 Full-page` &nbsp; `🎯 Desktop·Tablet·Mobile` &nbsp; `🌐 Web UI` &nbsp; `🖥️ CLI` &nbsp; `📄 PDF` &nbsp; `🖼️ WebP/AVIF` &nbsp; `⚡ Live progress` &nbsp; `🕒 Adjustable delays` &nbsp; `🚫 Popup blocking` &nbsp; `🎭 Hide elements` &nbsp; `⏳ Wait for selector` &nbsp; `🔄 Concurrency` &nbsp; `🔌 REST API` &nbsp; `🏷️ Custom naming` &nbsp; `📦 Standalone binary`

Capture, archive, and automate screenshots of websites in **PNG, WebP, AVIF, and PDF** formats — with advanced controls for delays, concurrency, and DOM manipulation. Perfect for portfolio archiving, legal records, and automation workflows.

<p align="center">
  <img src="assets/cybersnapper-screenshot.png" alt="CyberSnapper screenshot" width="700">
</p>

CyberSnapper is a powerful tool for capturing, archiving, and automating website screenshots. Originally built as an internal tool at CYBER BRAND for portfolio archiving, it has evolved into a full-featured website archiver with advanced controls for precision capture.

Use it for:
- **Portfolio archiving** (client websites, case studies).
- **Legal records** (save websites as PDFs for compliance).
- **Automation** (integrate with Zapier, n8n, or CI/CD pipelines).
- **Custom screenshots** (hide elements, wait for selectors, adjust delays).

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

The CLI loads settings from `config.json` and processes all URLs:

- **Delays**: `initialDelay`, `scrollDelay`, `finalDelay` (seconds).
- **Concurrency**: Number of websites to capture in parallel.
- **Formats**: Output formats (PNG, WebP, AVIF, PDF).
- **Advanced**: `hideSelectors`, `waitForSelector`, `blockPopups`.

## REST API

Capture screenshots programmatically:

```bash
curl "http://localhost:3000/api/screenshot?url=https://example.com&token=YOUR_TOKEN"
```

**Parameters**:
- `url`: Target website (required).
- `format`: Output format (`png`, `webp`, `avif`, `pdf`).
- `token`: API token from `config.json` (required).

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
| ⏱️ **Delays** | Adjust initial, scroll, and final delays for optimal loading |
| 🚫 **Popup blocking** | Toggle to block popups/modals (checkbox) |
| 🎭 **Hide elements** | Hide specific elements before capturing (CSS selectors) |
| ⏳ **Wait for selector** | Wait for a specific element before capturing |
| 🔄 **Concurrency** | Capture multiple websites in parallel |
| 🖼️ **Formats** | Choose output formats (PNG, WebP, AVIF, PDF) |
| 🔌 **REST API** | Integrate with Zapier, n8n, or CI/CD pipelines |
| 🏷️ **Naming** | Custom output filenames with variables (`{hostname}`, `{preset}`, `{width}`, `{height}`, `{domain}`, `{date}`, `{time}`, `{index}`) |
| 🖼️ **Gallery** | Thumbnail previews of every screenshot, click to open |
| 📁 **Open folder** | Reveals screenshots in your file manager |

---

## Configuration

Edit `config.json` to customize presets, delays, formats, and advanced settings:

```json
{
  "presets": [
    { "name": "Desktop", "width": 1920, "height": 1080 },
    { "name": "Tablet",  "width": 768,  "height": 1024 },
    { "name": "Mobile",  "width": 375,  "height": 812 }
  ],
  "initialDelay": 1.5,
  "scrollDelay": 1.8,
  "finalDelay": 1.0,
  "concurrency": 1,
  "formats": ["png"],
  "webp": { "quality": 80 },
  "avif": { "quality": 50 },
  "pdf": {
    "format": "A4",
    "landscape": false,
    "margin": "0"
  },
  "hideSelectors": [],
  "waitForSelector": "",
  "blockPopups": false,
  "apiToken": "generated_on_first_run",
  "naming": {
    "template": "{hostname}-{preset}"
  }
}
```

Changes are saved automatically from the Web UI.

### Capture Settings

| Setting            | Default | Purpose                                  |
|--------------------|---------|------------------------------------------|
| `initialDelay`     | 1.5s    | Wait before scrolling (above-the-fold).  |
| `scrollDelay`      | 1.8s    | Wait between scroll steps.               |
| `finalDelay`       | 1.0s    | Wait after scrolling back to top.        |
| `concurrency`      | 1       | Number of websites to capture in parallel. |

### Output Formats

- **PNG**: Lossless, high quality (default).
- **WebP**: Smaller files, good quality (quality: 1-100).
- **AVIF**: Even smaller files, modern format (quality: 1-100).
- **PDF**: For archiving/legal records (A4/Letter, portrait/landscape).

### Advanced Controls

| Setting            | Purpose                                  |
|--------------------|------------------------------------------|
| `hideSelectors`    | CSS selectors to hide before capturing.  |
| `waitForSelector`  | Wait for this selector before capturing. |
| `blockPopups`      | Block popups/modals (checkbox).          |

### REST API

- **Endpoint**: `GET /api/screenshot?url=...&token=...`
- **Authentication**: Requires `apiToken` from `config.json`.
- **Parameters**:
  - `url`: Target website.
  - `format`: Output format (`png`, `webp`, `avif`, `pdf`).
- **Example**:
  ```bash
  curl "http://localhost:3000/api/screenshot?url=https://example.com&token=YOUR_TOKEN"
  ```

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
