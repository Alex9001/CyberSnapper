/* ============================================================
   CyberSnapper — client logic
   ============================================================ */

const $ = (id) => document.getElementById(id);

const els = {
  urlInput: $('urls-input'),
  snapBtn: $('snap-btn'),
  themeToggle: $('theme-toggle'),
  helpBtn: $('help-btn'),
  helpModal: $('help-modal'),
  closeHelp: $('close-help'),
  stopBtn: $('stop-btn'),
  progress: $('progress'),
  results: $('results'),
  doneCount: $('done-count'),
  snapCount: $('snap-count'),
  openFolderBtn: $('open-folder-btn'),

  presetList: $('preset-list'),
  newName: $('new-name'),
  newWidth: $('new-width'),
  newHeight: $('new-height'),
  addBtn: $('add-preset-btn'),
  resetBtn: $('reset-presets-btn'),
  namingInput: $('naming-template'),
  namingPreview: $('naming-preview'),
  previewBtn: $('preview-btn'),
  initialDelay: $('initial-delay'),
  scrollDelay: $('scroll-delay'),
  concurrency: $('concurrency'),
  blockPopups: $('block-popups'),
  stripWhitespace: $('strip-whitespace'),
  formatPng: $('format-png'),
  formatWebp: $('format-webp'),
  formatAvif: $('format-avif'),
  formatPdf: $('format-pdf'),
  webpQuality: $('webp-quality'),
  avifQuality: $('avif-quality'),
  pdfFormat: $('pdf-format'),
  pdfLandscape: $('pdf-landscape'),
  pdfMargin: $('pdf-margin'),
  hideSelectors: $('hide-selectors'),
  waitForSelector: $('wait-for-selector'),
  blocklistTextarea: $('blocklist-textarea'),
};

let presets = [];
let captureViewports = [];
let totalSnaps = 0;
let currentUrlIndex = 0;
let saving = false;

/* ---------- Theme ----------
   Dark is the default. The user's preference is persisted in config.json
   (via PUT /config) — not localStorage or browser preferences. */
const state = { theme: 'dark' };

function applyThemeClass(t) {
  document.documentElement.className = (t === 'light') ? 'light' : 'dark';
}

function setTheme(t) {
  state.theme = (t === 'light') ? 'light' : 'dark';
  applyThemeClass(state.theme);
  saveConfig();
}

applyThemeClass('dark'); /* avoid FOUC before config loads */
els.themeToggle.addEventListener('click', () =>
  setTheme(state.theme === 'dark' ? 'light' : 'dark'));

/* ---------- Helpers ---------- */
function esc(t) {
  const d = document.createElement('div');
  d.textContent = t == null ? '' : String(t);
  return d.innerHTML;
}
function slug(s) { return String(s).replace(/[^a-z0-9]/gi, '_').toLowerCase(); }
function clampNum(value, fallback, min, max) {
  const n = parseFloat(value);
  if (Number.isNaN(n)) return fallback;
  if (n < min) return min;
  if (n > max) return max;
  return n;
}

/* ---------- Config ---------- */
function collectConfig() {
  const formats = [];
  if (els.formatPng.checked) formats.push('png');
  if (els.formatWebp.checked) formats.push('webp');
  if (els.formatAvif.checked) formats.push('avif');
  if (els.formatPdf.checked) formats.push('pdf');

  const hideSelectors = els.hideSelectors.value
    .split('\n')
    .map(s => s.trim())
    .filter(s => s.length > 0);

  const blocklist = els.blocklistTextarea.value
    .split('\n')
    .map(s => s.trim())
    .filter(s => s.length > 0);

  return {
    presets,
    naming: { template: els.namingInput.value.trim() || '{hostname}-{preset}' },
    initialDelay: clampNum(els.initialDelay.value, 1.5, 0.1, 60),
    scrollDelay: clampNum(els.scrollDelay.value, 1.8, 0.1, 60),
    concurrency: clampNum(els.concurrency.value, 1, 1, 10),
    formats,
    webp: { quality: parseInt(els.webpQuality.value, 10) || 80 },
    avif: { quality: parseInt(els.avifQuality.value, 10) || 50 },
    pdf: {
      format: els.pdfFormat.value,
      landscape: els.pdfLandscape.checked,
      margin: els.pdfMargin.value,
    },
    hideSelectors,
    waitForSelector: els.waitForSelector.value.trim(),
    blockPopups: els.blockPopups.checked,
    stripWhitespace: els.stripWhitespace.checked,
    blocklist,
    theme: state.theme,
  };
}

function applyConfig(data) {
  presets = Array.isArray(data.presets) ? data.presets : [];
  if (data.naming && data.naming.template) {
    els.namingInput.value = data.naming.template;
  }
  if (data.initialDelay != null) els.initialDelay.value = data.initialDelay;
  if (data.scrollDelay != null) els.scrollDelay.value = data.scrollDelay;
  if (data.concurrency != null) els.concurrency.value = data.concurrency;
  if (data.blockPopups != null) els.blockPopups.checked = data.blockPopups;
  if (data.stripWhitespace != null) els.stripWhitespace.checked = data.stripWhitespace;

  if (data.theme === 'light' || data.theme === 'dark') {
    state.theme = data.theme;
    applyThemeClass(state.theme);
  }

  if (Array.isArray(data.formats)) {
    els.formatPng.checked = data.formats.includes('png');
    els.formatWebp.checked = data.formats.includes('webp');
    els.formatAvif.checked = data.formats.includes('avif');
    els.formatPdf.checked = data.formats.includes('pdf');
  }
  if (data.webp) els.webpQuality.value = data.webp.quality;
  if (data.avif) els.avifQuality.value = data.avif.quality;
  if (data.pdf) {
    els.pdfFormat.value = data.pdf.format;
    els.pdfLandscape.checked = !!data.pdf.landscape;
    els.pdfMargin.value = data.pdf.margin;
  }
  if (Array.isArray(data.hideSelectors)) {
    els.hideSelectors.value = data.hideSelectors.join('\n');
  }
  if (Array.isArray(data.blocklist)) {
    els.blocklistTextarea.value = data.blocklist.join('\n');
  }
  if (data.waitForSelector != null) {
    els.waitForSelector.value = data.waitForSelector;
  }

  renderPresets();
  updatePreview();
}

async function loadConfig() {
  try {
    const res = await fetch('/config');
    applyConfig(await res.json());
  } catch (err) {
    console.error('Failed to load config:', err);
  }
}

async function saveConfig() {
  if (saving) return;
  saving = true;
  try {
    await fetch('/config', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(collectConfig()),
    });
  } catch (err) {
    console.error('Failed to save config:', err);
  } finally {
    saving = false;
  }
}

/* ---------- Presets ---------- */
function renderPresets() {
  els.presetList.innerHTML = presets.map((p, i) =>
    '<label class="preset-chip selected">' +
    '<input type="checkbox" checked data-i="' + i + '">' +
    esc(p.name) + ' <span class="dim">' + p.width + '×' + p.height + '</span>' +
    '<span class="del" data-i="' + i + '">✕</span></label>'
  ).join('');

  els.presetList.querySelectorAll('input[type=checkbox]').forEach(cb =>
    cb.addEventListener('change', () =>
      cb.closest('.preset-chip').classList.toggle('selected', cb.checked)));

  els.presetList.querySelectorAll('.del').forEach(el =>
    el.addEventListener('click', async (e) => {
      e.preventDefault();
      const i = +e.target.dataset.i;
      presets.splice(i, 1);
      await saveConfig();
      renderPresets();
    }));
}

els.addBtn.addEventListener('click', async () => {
  const name = els.newName.value.trim();
  const w = parseInt(els.newWidth.value, 10);
  const h = parseInt(els.newHeight.value, 10);
  if (!name || isNaN(w) || w <= 0 || isNaN(h) || h <= 0) return;
  if (presets.some(p => p.name.toLowerCase() === name.toLowerCase())) return;

  presets.push({ name, width: w, height: h });
  await saveConfig();
  els.newName.value = ''; els.newWidth.value = ''; els.newHeight.value = '';
  renderPresets();
});

els.resetBtn.addEventListener('click', async () => {
  await fetch('/config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      presets: [],
      naming: { template: els.namingInput.value.trim() || '{hostname}-{preset}' },
    }),
  });
  await loadConfig();
});

/* Numeric input guards */
[els.newName, els.newWidth, els.newHeight].forEach(el => {
  el.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); els.addBtn.click(); return; }
    if (el.id === 'new-name') return;
    if (!/[0-9]/.test(e.key) &&
        !['Backspace', 'Delete', 'ArrowLeft', 'ArrowRight', 'Tab'].includes(e.key)) {
      e.preventDefault();
    }
  });
});

els.initialDelay.addEventListener('change', () => { clampNum(els.initialDelay.value, 1.5, 0.1, 60); saveConfig(); });
els.scrollDelay.addEventListener('change', () => { clampNum(els.scrollDelay.value, 1.8, 0.1, 60); saveConfig(); });
els.concurrency.addEventListener('change', () => { clampNum(els.concurrency.value, 1, 1, 10); saveConfig(); });

[els.formatPng, els.formatWebp, els.formatAvif, els.formatPdf].forEach(cb => {
  cb.addEventListener('change', () => {
    if (!els.formatPng.checked && !els.formatWebp.checked && !els.formatAvif.checked && !els.formatPdf.checked) {
      els.formatPng.checked = true;
    }
    saveConfig();
  });
});

[els.webpQuality, els.avifQuality].forEach(input => {
  input.addEventListener('change', () => {
    const v = parseInt(input.value, 10);
    if (isNaN(v) || v < 1 || v > 100) {
      input.value = input.id === 'webp-quality' ? 80 : 50;
    }
    saveConfig();
  });
});

[els.pdfFormat, els.pdfLandscape, els.pdfMargin].forEach(el =>
  el.addEventListener('change', saveConfig));

els.hideSelectors.addEventListener('change', saveConfig);
els.waitForSelector.addEventListener('change', saveConfig);
els.blockPopups.addEventListener('change', saveConfig);
els.stripWhitespace.addEventListener('change', saveConfig);

/* ---------- Naming preview ---------- */
document.querySelectorAll('.nv').forEach(el =>
  el.addEventListener('click', () => {
    const v = el.dataset.v;
    const start = els.namingInput.selectionStart;
    const end = els.namingInput.selectionEnd;
    const val = els.namingInput.value;
    els.namingInput.value = val.slice(0, start) + v + val.slice(end);
    els.namingInput.selectionStart = els.namingInput.selectionEnd = start + v.length;
    els.namingInput.focus();
    updatePreview();
  }));

document.querySelectorAll('[data-template]').forEach(el =>
  el.addEventListener('click', () => {
    els.namingInput.value = el.dataset.template;
    updatePreview();
  }));

els.namingInput.addEventListener('input', updatePreview);
els.previewBtn.addEventListener('click', updatePreview);

async function updatePreview() {
  const template = els.namingInput.value.trim() || '{hostname}-{preset}';
  const firstUrlLine = els.urlInput.value.trim().split('\n').map(l => l.trim()).filter(Boolean)[0];
  const sampleUrl = firstUrlLine || 'https://example.com';
  const samplePreset = presets.length > 0 ? presets[0] : { name: 'Desktop', width: 1920, height: 1080 };

  try {
    const res = await fetch('/preview', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ template, url: sampleUrl, preset: samplePreset }),
    });
    const data = await res.json();
    els.namingPreview.textContent = '▸ screenshots/' + (data.preview || '');
  } catch {
    els.namingPreview.textContent = '';
  }
}

/* ---------- Capture ---------- */
els.snapBtn.addEventListener('click', startCapture);

async function startCapture() {
  const urls = els.urlInput.value
    .trim()
    .split('\n')
    .map(l => l.trim())
    .filter(l => l.length > 0);
  if (urls.length === 0) return;

  const selected = presets.filter((_, i) => {
    const cb = els.presetList.querySelector('input[data-i="' + i + '"]');
    return cb ? cb.checked : true;
  });
  if (selected.length === 0) return;

  const payload = collectConfig();
  payload.urls = urls;
  payload.presets = selected;

  els.snapBtn.disabled = true;
  els.snapBtn.textContent = '⌁ CAPTURING';
  els.progress.classList.add('active');
  captureViewports = selected;
  els.results.innerHTML = '';
  totalSnaps = 0;
  currentUrlIndex = 0;
  els.snapCount.style.display = 'none';
  els.openFolderBtn.classList.remove('visible');
  els.doneCount.textContent = '0';

  try {
    const res = await fetch('/capture', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });

    if (!res.ok) {
      els.results.innerHTML += '<div class="url-result">Server error: ' +
        esc((await res.text()) || res.status) + '</div>';
      return;
    }

    await streamEvents(res, handleEvent);
  } catch (err) {
    els.results.innerHTML += '<div class="url-result">Connection error: ' +
      esc(err.message) + '</div>';
  } finally {
    els.snapBtn.disabled = false;
    els.snapBtn.textContent = 'SNAP';
  }
}

async function streamEvents(res, onEvent) {
  const reader = res.body.getReader();
  const dec = new TextDecoder();
  let buf = '';
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    buf += dec.decode(value, { stream: true });
    const lines = buf.split('\n');
    buf = lines.pop() || '';
    for (const l of lines) {
      if (!l.startsWith('data: ')) continue;
      try {
        onEvent(JSON.parse(l.slice(6)));
      } catch {
        /* keep going on malformed line */
      }
    }
  }
}

function handleEvent(ev) {
  switch (ev.type) {
    case 'url-start': {
      currentUrlIndex = ev.index;
      const d = document.createElement('div');
      d.className = 'url-result';
      d.id = 'url-' + ev.index;

      let framesHtml = '';
      for (const vp of captureViewports) {
        framesHtml +=
          '<div class="vp-frame vp-frame-pending" id="vp-frame-' + ev.index + '-' + slug(vp.name) + '">' +
          '<div class="vp-frame-label">' + esc(vp.name) + '</div>' +
          '<div class="vp-frame-thumb">' +
          '<div class="vp-frame-placeholder">' +
          '<span class="vp-frame-placeholder-icon">⊘</span>' +
          '<span class="vp-frame-placeholder-text">NO IMAGE</span>' +
          '</div></div></div>';
      }

      d.innerHTML =
        '<div class="url-body">' +
        '<div class="url-head">' +
        '<div class="url-line"><span class="status-icon">⏳</span>' +
        '<span class="url-text">' + esc(ev.url) + '</span>' +
        '<span class="badge">' + (ev.index + 1) + '/' + ev.total + '</span></div>' +
        '</div>' +
        '<div class="vp-frames" id="vp-frames-' + ev.index + '">' + framesHtml + '</div>' +
        '</div>';
      els.results.appendChild(d);
      break;
    }
    case 'url-error': {
      const el = document.getElementById('url-' + (ev.index != null ? ev.index : currentUrlIndex));
      if (el) {
        el.querySelector('.status-icon').textContent = '✕';
        el.querySelector('.badge').textContent = 'invalid';
        el.querySelector('.badge').className = 'badge error';
      }
      break;
    }
    case 'url-done': {
      const idx = ev.index != null ? ev.index : currentUrlIndex;
      const el = document.getElementById('url-' + idx);
      if (el) {
        el.querySelector('.status-icon').textContent = '✓';
        el.querySelector('.badge').className = 'badge done';
        el.querySelector('.badge').textContent = 'done';
      }
      break;
    }
    case 'viewport-start': {
      const idx = ev.index != null ? ev.index : currentUrlIndex;
      const frame = document.getElementById('vp-frame-' + idx + '-' + slug(ev.viewport));
      if (frame) {
        frame.className = 'vp-frame vp-frame-loading';
      }
      break;
    }
    case 'viewport-error': {
      const idx = ev.index != null ? ev.index : currentUrlIndex;
      const frame = document.getElementById('vp-frame-' + idx + '-' + slug(ev.viewport));
      if (frame) {
        frame.className = 'vp-frame vp-frame-error';
        const thumb = frame.querySelector('.vp-frame-thumb');
        if (thumb) {
          thumb.innerHTML = '<div class="vp-frame-placeholder"><span class="vp-frame-placeholder-icon" style="opacity:0.6;color:var(--red)">✕</span><span class="vp-frame-placeholder-text" style="color:var(--red)">FAILED</span></div>';
        }
      }
      break;
    }
    case 'viewport-done': {
      const idx = ev.index != null ? ev.index : currentUrlIndex;
      const fn = (ev.file || '').split(/[/\\]/).pop();
      const frame = document.getElementById('vp-frame-' + idx + '-' + slug(ev.viewport));
      if (frame) {
        frame.className = 'vp-frame vp-frame-done';
        const thumb = frame.querySelector('.vp-frame-thumb');
        if (thumb) {
          thumb.innerHTML = '<img src="/screenshots/' + encodeURIComponent(fn) + '" alt="' + esc(fn) + '">';
        }
        frame.addEventListener('click', (e) => {
          e.stopPropagation();
          openLightbox(fn);
        });
      }
      totalSnaps++;
      els.doneCount.textContent = totalSnaps;
      els.snapCount.style.display = 'block';
      els.openFolderBtn.classList.add('visible');
      break;
    }
    case 'warning': {
      const line = document.createElement('div');
      line.className = 'warning-line';
      line.style.color = 'var(--gold)';
      line.style.fontSize = '11px';
      line.style.marginBottom = '4px';
      line.textContent = '⚠ ' + ev.message;
      els.results.appendChild(line);
      break;
    }
    case 'error': {
      const line = document.createElement('div');
      line.style.color = 'var(--red)';
      line.style.fontSize = '12px';
      line.textContent = '✕ ' + (ev.message || 'error');
      els.results.appendChild(line);
      break;
    }
    case 'status': {
      let statusEl = document.getElementById('install-status');
      if (!statusEl) {
        statusEl = document.createElement('div');
        statusEl.id = 'install-status';
        statusEl.className = 'install-status';
        statusEl.innerHTML =
          '<div class="install-status-spinner"></div>' +
          '<span class="install-status-text" id="install-status-msg"></span>' +
          '<div class="install-status-bar"><div class="install-status-fill" id="install-status-fill"></div></div>';
        els.results.prepend(statusEl);
      }
      const msg = document.getElementById('install-status-msg');
      const fill = document.getElementById('install-status-fill');
      msg.textContent = ev.message;
      if (ev.percent != null) {
        fill.style.width = Math.min(100, Math.max(0, ev.percent)) + '%';
      }
      break;
    }
    case 'done': {
      const statusEl = document.getElementById('install-status');
      if (statusEl) statusEl.remove();
      break;
    }
  }
}

/* ---------- Lightbox ---------- */
const lightbox = document.getElementById('lightbox');
const lightboxImg = document.getElementById('lightbox-img');
const lightboxFilename = document.getElementById('lightbox-filename');

function openLightbox(filename) {
  lightboxImg.src = '/screenshots/' + encodeURIComponent(filename);
  lightboxFilename.textContent = filename;
  lightbox.classList.add('visible');
}

function closeLightbox() {
  lightbox.classList.remove('visible');
  lightboxImg.src = '';
  lightboxFilename.textContent = '';
}

document.getElementById('lightbox-close').addEventListener('click', closeLightbox);
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') closeLightbox();
});

/* Click outside the modal window closes the lightbox */
lightbox.addEventListener('click', (e) => {
  if (e.target === lightbox) closeLightbox();
});

/* ---------- Misc ---------- */
els.openFolderBtn.addEventListener('click', () => fetch('/open-folder').catch(() => {}));
els.stopBtn.addEventListener('click', () => {
  if (confirm('Stop the server and exit CyberSnapper?')) {
    fetch('/shutdown').catch(() => {});
  }
});

/* Help modal */
els.helpBtn.addEventListener('click', () => els.helpModal.classList.add('visible'));
els.closeHelp.addEventListener('click', () => els.helpModal.classList.remove('visible'));
els.helpModal.addEventListener('click', (e) => {
  if (e.target === els.helpModal) els.helpModal.classList.remove('visible');
});
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') els.helpModal.classList.remove('visible');
});

els.blocklistTextarea.addEventListener('change', saveConfig);

/* ---------- Auto-stop toast + lifecycle hooks ----------
   Server auto-stops after 15 min idle. We poll /keepalive to detect the
   warning window (last 60s) and show a toast with a live countdown.
   Closing the tab triggers /shutdown via beforeunload. */

const toast = document.createElement('div');
toast.id = 'autostop-toast';
toast.className = 'autostop-toast';
toast.innerHTML =
  '<div class="autostop-text">Auto-stop in <strong id="autostop-clock">--:--</strong></div>' +
  '<button class="autostop-keep" id="autostop-keep">Keep alive</button>';
document.body.appendChild(toast);

const toastKeep = toast.querySelector('#autostop-keep');
const clockEl = toast.querySelector('#autostop-clock');

function fmt(ms) {
  const s = Math.max(0, Math.ceil(ms / 1000));
  const m = Math.floor(s / 60);
  const r = s % 60;
  return m + ':' + String(r).padStart(2, '0');
}

let keepalivePoll = null;
function startKeepalivePolling() {
  if (keepalivePoll) return;
  keepalivePoll = setInterval(async () => {
    try {
      const res = await fetch('/keepalive');
      if (!res.ok) return;
      const d = await res.json();
      if (d.warning) {
        clockEl.textContent = fmt(d.remainingMs);
        toast.classList.add('visible');
      } else {
        toast.classList.remove('visible');
      }
    } catch { /* server may be shutting down */ }
  }, 1000);
}
startKeepalivePolling();

toastKeep.addEventListener('click', async () => {
  try { await fetch('/keepalive', { method: 'POST' }); } catch {}
  toast.classList.remove('visible');
});

/* Tab close → shut down server. Also fires on refresh, which is acceptable:
   refresh restarts the server via the launcher anyway. */
window.addEventListener('beforeunload', () => {
  try {
    const url = '/shutdown';
    fetch(url, { keepalive: true }).catch(() => {});
  } catch {}
});

/* ---------- Boot ---------- */
loadConfig();
