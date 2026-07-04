# Screenshot Bot

A Node.js tool using Playwright to take full-page screenshots of a list of websites in three responsive resolutions (Desktop, Tablet, Mobile). The tool automatically scrolls down the page to trigger lazy-loaded images and waits for network idle before capturing the image.

## Requirements
- Node.js

## Installation

1. Install dependencies:
   ```bash
   npm install
   ```
2. Install Playwright browsers (if you haven't already):
   ```bash
   npx playwright install chromium
   ```

## Usage

You can feed a list of websites to the script either by passing them as arguments or by using a text file.

### 1. Using a text file (Recommended)
Create a `.txt` file with one URL per line (e.g., the included `urls.txt`), and pass it to the script.

```bash
node capture.js urls.txt
```

### 2. Passing URLs directly
Pass URLs separated by spaces directly into the command.

```bash
node capture.js https://example.com https://google.com
```

### 3. Using the Shell Script
A convenience script `run.sh` is provided. It installs dependencies and runs the bot against `urls.txt`.
```bash
# Make it executable first
chmod +x run.sh

# Run it
./run.sh
```

## How it works
For each URL, the script will:
- Load the site at 3 different viewports (Desktop: 1920x1080, Tablet: 768x1024, Mobile: 375x812).
- Scroll organically down the page to trigger lazy-loaded assets.
- Wait for network idle.
- Capture a full-height PNG.
- Name the screenshots using the domain name (e.g., `example_com-desktop.png`).
