import crypto from 'node:crypto';
import path from 'node:path';
import { access, mkdir, rename, statfs, writeFile } from 'node:fs/promises';
import { chromium, firefox, webkit, type Browser, type BrowserContext, type BrowserType, type Page } from 'playwright';
import sharp from 'sharp';
import { assertPublicUrl, startFilteringProxy, type NetworkPolicy } from './network.js';
import { ContentBlocker } from './blocking.js';
import { captureName, OutputPathAllocator, safeSegment } from './naming.js';
import { normalizePresentation, renderPresentation } from './presentation.js';
import type { Artifact, BrowserEngine, CaptureJob, ColorScheme, OutputFormat, TargetSnapshot, Viewport, WorkerEvent } from './protocol.js';
import { windowsBinaryIsX64 } from './windows.js';

export interface JobRuntime {
  cancelled: boolean;
  browsers: Set<Browser>;
}

type Emit = (event: Omit<WorkerEvent, 'protocolVersion' | 'sequence' | 'timestamp' | 'jobId'>) => void;

interface CaptureTarget { index: number; position: number; totalTargets: number; colorScheme: ColorScheme; target: TargetSnapshot; engine: BrowserEngine; viewport: Viewport; formats: OutputFormat[]; }

const browserTypes: Record<BrowserEngine, BrowserType> = { chromium, firefox, webkit };

async function launchEngine(engine: BrowserEngine, proxyUrl: string): Promise<Browser> {
  const args = engine === 'chromium' ? ['--proxy-bypass-list=<-loopback>'] : [];
  if (engine === 'chromium') {
    return launchChromiumWithSystemFallback(proxyUrl, args);
  }
  return browserTypes[engine].launch({ headless: true, proxy: { server: proxyUrl }, args });
}

async function launchChromiumWithSystemFallback(proxyUrl: string, args: string[]): Promise<Browser> {
  const executablePath = chromium.executablePath();
  const bundled = await access(executablePath).then(() => true).catch(() => false);
  // On Windows arm64 a bundled x64 Chromium runs under emulation, so prefer a
  // native system Chrome or Edge when one is available.
  const preferSystem = bundled && process.platform === 'win32' && process.arch === 'arm64'
    && await windowsBinaryIsX64(executablePath);
  if (bundled && !preferSystem) {
    return chromium.launch({ headless: true, proxy: { server: proxyUrl }, args });
  }
  for (const channel of ['chrome', 'msedge']) {
    try {
      return await chromium.launch({ headless: true, proxy: { server: proxyUrl }, args, channel });
    } catch {
      // The channel browser is not installed; try the next system browser.
    }
  }
  if (bundled) {
    // No native system browser found; keep the bundled (possibly emulated) Chromium.
    return chromium.launch({ headless: true, proxy: { server: proxyUrl }, args });
  }
  throw new Error('Chromium is not installed and no system browser (Google Chrome or Microsoft Edge) is available. Install Chromium from Settings, or install Chrome or Edge.');
}

const maximumArtifacts = 10_000;
const maximumDevicePixels = 64_000_000;
const maximumConcurrentDevicePixels = 128_000_000;
const minimumFreeBytes = 256 * 1024 * 1024;

const sleep = (seconds: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, Math.max(0, seconds) * 1000));
const id = (): string => crypto.randomUUID();
const sha256 = (data: Buffer): string => crypto.createHash('sha256').update(data).digest('hex');

function inside(root: string, candidate: string): boolean {
  const relative = path.relative(root, candidate);
  return relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
}

async function atomicWrite(destination: string, bytes: Buffer): Promise<void> {
  const disk = await statfs(path.dirname(destination));
  const available = Number(disk.bavail) * Number(disk.bsize);
  if (!Number.isFinite(available) || available < Math.max(minimumFreeBytes, bytes.length * 2)) {
    throw new Error('Not enough free disk space to safely write the capture');
  }
  const temporary = `${destination}.${process.pid}.${crypto.randomBytes(4).toString('hex')}.tmp`;
  await writeFile(temporary, bytes);
  await rename(temporary, destination);
}

export type RouteDecision =
  | { action: 'abort'; reason: 'blocklist' | 'policy' | 'filter' }
  | { action: 'continue' };

interface RoutableRequest {
  url(): string;
  resourceType(): string;
  isNavigationRequest(): boolean;
  frame(): { parentFrame(): unknown } | null;
}

/**
 * Single source of truth for request routing order: user blocklist first,
 * then the network-security policy, then content-blocking filters. Community
 * filters only ever reject subresources, so exceptions cannot bypass the
 * capture boundary, and the main document is never community-blocked.
 */
export async function decideRoute(
  request: RoutableRequest,
  blocklist: string[],
  policy: NetworkPolicy,
  blocker: ContentBlocker | undefined,
  securityCheck: (url: string, checkPolicy: NetworkPolicy) => Promise<void> = assertPublicUrl,
): Promise<RouteDecision> {
  const requestUrl = request.url();
  if (blocklist.some((part) => part && requestUrl.includes(part))) {
    return { action: 'abort', reason: 'blocklist' };
  }
  if (!requestUrl.startsWith('http:') && !requestUrl.startsWith('https:')) return { action: 'continue' };
  try {
    await securityCheck(requestUrl, policy);
  } catch {
    return { action: 'abort', reason: 'policy' };
  }
  if (blocker?.enabled) {
    const frame = request.frame();
    const mainDocument = request.isNavigationRequest() && frame !== null && frame.parentFrame() === null;
    if (!mainDocument && blocker.blocksSubresource(requestUrl, request.resourceType())) {
      blocker.metrics.blockedSubresources += 1;
      return { action: 'abort', reason: 'filter' };
    }
  }
  return { action: 'continue' };
}

async function installRouting(context: BrowserContext, blocklist: string[], policy: NetworkPolicy,
                              blocker: ContentBlocker | undefined): Promise<void> {
  await context.route('**/*', async (route) => {
    const decision = await decideRoute(route.request(), blocklist, policy, blocker);
    if (decision.action === 'continue') return route.continue();
    return route.abort('blockedbyclient');
  });
}

async function hideElements(page: Page, selectors: string[]): Promise<void> {
  for (const selector of selectors) {
    if (!selector.trim()) continue;
    try {
      await page.locator(selector).evaluateAll((elements) => {
        for (const element of elements) {
          if (element instanceof HTMLElement) element.style.setProperty('visibility', 'hidden', 'important');
        }
      });
    } catch { /* Invalid optional selectors do not fail a capture. */ }
  }
}

async function autoScroll(page: Page, maximumSeconds: number): Promise<void> {
  await page.evaluate(async (maximumMs) => {
    const started = Date.now();
    let previousHeight = 0;
    let stable = 0;
    while (Date.now() - started < maximumMs && stable < 4) {
      const height = Math.max(document.documentElement.scrollHeight, document.body?.scrollHeight ?? 0);
      window.scrollTo(0, height);
      await new Promise((resolve) => setTimeout(resolve, 250));
      const next = Math.max(document.documentElement.scrollHeight, document.body?.scrollHeight ?? 0);
      stable = next === previousHeight ? stable + 1 : 0;
      previousHeight = next;
    }
    window.scrollTo(0, 0);
  }, Math.max(1000, maximumSeconds * 1000));
}

async function screenshotPng(page: Page, job: CaptureJob, viewport: Viewport): Promise<Buffer> {
  const profile = job.profile;
  if (profile.captureMode === 'element') {
    const locator = page.locator(profile.elementSelector).first();
    await locator.waitFor({ state: 'visible', timeout: profile.selectorTimeoutSeconds * 1000 });
    const box = await locator.boundingBox();
    if (box) ensurePixelBudget(box.width, box.height, viewport.deviceScaleFactor);
    return Buffer.from(await locator.screenshot({ type: 'png', animations: 'disabled' }));
  }
  if (profile.captureMode === 'viewport') {
    ensurePixelBudget(viewport.width, viewport.height, viewport.deviceScaleFactor);
    return Buffer.from(await page.screenshot({ type: 'png', animations: 'disabled' }));
  }
  const pageHeight = await page.evaluate(() => Math.max(document.documentElement.scrollHeight, document.body?.scrollHeight ?? 0));
  ensurePixelBudget(viewport.width, Math.min(pageHeight, profile.maxPageHeight), viewport.deviceScaleFactor);
  if (pageHeight <= profile.maxPageHeight) {
    return Buffer.from(await page.screenshot({ type: 'png', fullPage: true, animations: 'disabled' }));
  }
  return Buffer.from(await page.screenshot({ type: 'png', animations: 'disabled',
    clip: { x: 0, y: 0, width: viewport.width, height: profile.maxPageHeight } }));
}

function ensurePixelBudget(width: number, height: number, scale: number): void {
  const pixels = Math.ceil(width * scale) * Math.ceil(height * scale);
  if (!Number.isFinite(pixels) || pixels > maximumDevicePixels) {
    throw new Error(`Capture exceeds the ${maximumDevicePixels.toLocaleString()} device-pixel safety limit`);
  }
}

async function convertPng(png: Buffer, format: OutputFormat, job: CaptureJob): Promise<Buffer> {
  if (format === 'png') return png;
  if (format === 'webp') return sharp(png).webp({ quality: job.profile.webpQuality }).toBuffer();
  if (format === 'avif') return sharp(png).avif({ quality: job.profile.avifQuality }).toBuffer();
  throw new Error(`Cannot convert PNG to ${format}`);
}

async function stripTopWhitespace(png: Buffer): Promise<Buffer> {
  const metadata = await sharp(png).metadata();
  if (!metadata.width || !metadata.height) return png;
  const { width, height } = metadata;
  if (width * height > maximumDevicePixels) throw new Error('Image exceeds the processing pixel limit');
  const raw = await sharp(png).ensureAlpha().raw().toBuffer();
  const left = Math.floor(width * 0.2);
  const right = Math.ceil(width * 0.8);
  const step = Math.max(1, Math.floor((right - left) / 40));
  let firstContent = -1;
  for (let y = 0; y < height; y += 1) {
    let nonWhite = 0;
    for (let x = left; x < right; x += step) {
      const offset = (y * width + x) * 4;
      if (raw[offset + 3] > 8 && (raw[offset] < 248 || raw[offset + 1] < 248 || raw[offset + 2] < 248)) nonWhite += 1;
    }
    if (nonWhite >= 3) { firstContent = y; break; }
  }
  if (firstContent < 2 || firstContent >= height) return png;
  return sharp(png).extract({ left: 0, top: firstContent, width, height: height - firstContent }).png().toBuffer();
}

async function compareArtifact(job: CaptureJob, artifact: Artifact, output: Buffer,
                               comparisonKey: string, emit: Emit): Promise<void> {
  const baseline = job.baselines?.[comparisonKey];
  const common = {
    id: id(), jobId: job.id, comparisonKey, currentArtifactId: artifact.id,
    url: artifact.url, targetId: artifact.targetId ?? '', targetName: artifact.targetName ?? '',
    targetSetId: artifact.targetSetId ?? '', targetSetName: artifact.targetSetName ?? '',
    engine: artifact.engine, viewportId: artifact.viewportId, viewportName: artifact.viewportName,
    captureMode: artifact.captureMode, format: artifact.format, createdAt: new Date().toISOString(),
  };
  if (!baseline?.artifact?.relativePath) {
    emit({ type: 'comparison_completed', comparison: {
      ...common, baselineArtifactId: '', status: 'missing_baseline', mismatchRatio: 0,
      diffRelativePath: '', analysisWidth: artifact.width, analysisHeight: artifact.height,
      analysisScale: 1, mismatchedPixels: 0, analyzedPixels: 0, algorithmVersion: 2,
      changedRegions: [], baselineRelativePath: '',
    } });
    return;
  }
  const baselinePath = path.resolve(job.projectRoot, baseline.artifact.relativePath);
  if (!inside(path.resolve(job.projectRoot), baselinePath)) throw new Error('Baseline path escapes project');
  const leftMeta = await sharp(baselinePath).metadata();
  const rightMeta = await sharp(output).metadata();
  const sourceWidth = Math.max(leftMeta.width ?? 1, rightMeta.width ?? 1);
  const sourceHeight = Math.max(leftMeta.height ?? 1, rightMeta.height ?? 1);
  const maximumComparisonPixels = 8_000_000;
  const scale = Math.min(1, Math.sqrt(maximumComparisonPixels / (sourceWidth * sourceHeight)));
  const width = Math.max(1, Math.floor(sourceWidth * scale));
  const height = Math.max(1, Math.floor(sourceHeight * scale));
  const normalize = (input: string | Buffer, inputWidth: number, inputHeight: number) => {
    const pipeline = sharp(input).ensureAlpha();
    if (scale < 1) {
      return pipeline.resize({ width, height, fit: 'contain', background: { r: 255, g: 255, b: 255, alpha: 1 } })
        .raw().toBuffer();
    }
    return pipeline.extend({ right: width - inputWidth, bottom: height - inputHeight,
      background: { r: 255, g: 255, b: 255, alpha: 1 } }).raw().toBuffer();
  };
  const [left, right] = await Promise.all([
    normalize(baselinePath, leftMeta.width ?? 1, leftMeta.height ?? 1),
    normalize(output, rightMeta.width ?? 1, rightMeta.height ?? 1),
  ]);
  const pixels = width * height;
  let mismatched = 0;
  let minX = width;
  let minY = height;
  let maxX = -1;
  let maxY = -1;
  const diff = Buffer.alloc(pixels * 4);
  const channelThreshold = job.profile.pixelThreshold * 255;
  for (let pixel = 0; pixel < pixels; pixel += 1) {
    const offset = pixel * 4;
    const delta = Math.max(Math.abs(left[offset] - right[offset]), Math.abs(left[offset + 1] - right[offset + 1]),
                           Math.abs(left[offset + 2] - right[offset + 2]), Math.abs(left[offset + 3] - right[offset + 3]));
    if (delta > channelThreshold) {
      mismatched += 1;
      const x = pixel % width;
      const y = Math.floor(pixel / width);
      minX = Math.min(minX, x); minY = Math.min(minY, y);
      maxX = Math.max(maxX, x); maxY = Math.max(maxY, y);
      diff[offset] = 255; diff[offset + 1] = 0; diff[offset + 2] = 96; diff[offset + 3] = 255;
    } else {
      const gray = Math.round((right[offset] + right[offset + 1] + right[offset + 2]) / 6 + 96);
      diff[offset] = gray; diff[offset + 1] = gray; diff[offset + 2] = gray; diff[offset + 3] = 255;
    }
  }
  const mismatchRatio = mismatched / pixels;
  const diffDirectory = path.join(job.projectRoot, '.cybersnapper', 'diffs', job.id);
  await mkdir(diffDirectory, { recursive: true });
  const diffPath = path.join(diffDirectory, `${safeSegment(artifact.id)}.diff.png`);
  await sharp(diff, { raw: { width, height, channels: 4 } }).png().toFile(diffPath);
  const dimensionsChanged = leftMeta.width !== rightMeta.width || leftMeta.height !== rightMeta.height;
  const changedRegions = mismatched > 0
    ? [{ x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1, pixels: mismatched }]
    : [];
  emit({ type: 'comparison_completed', comparison: {
    ...common, baselineArtifactId: baseline.artifactId,
    status: dimensionsChanged ? 'dimensions_changed' : mismatchRatio > job.profile.mismatchThreshold ? 'changed' : 'matched',
    mismatchRatio, diffRelativePath: path.relative(job.projectRoot, diffPath),
    baselineWidth: leftMeta.width, baselineHeight: leftMeta.height,
    currentWidth: rightMeta.width, currentHeight: rightMeta.height,
    analysisWidth: width, analysisHeight: height, analysisScale: scale,
    mismatchedPixels: mismatched, analyzedPixels: pixels, algorithmVersion: 2, changedRegions,
    baselineRelativePath: baseline.artifact.relativePath,
  } });
}

async function captureTarget(job: CaptureJob, target: CaptureTarget, browser: Browser,
                             outputDirectory: string, allocator: OutputPathAllocator, runtime: JobRuntime,
                             outputEmit: Emit, lightImageHash?: string): Promise<{ completed: number; failed: number; imageHash?: string }> {
  let context: BrowserContext | undefined;
  let page: Page | undefined;
  let completed = 0;
  let failed = 0;
  let imageHash: string | undefined;
  let reusedExistingOutput = false;
  const { engine, viewport, formats, colorScheme } = target;
  const { url } = target.target;
  const emit: Emit = (event) => outputEmit({ ...event,
    ...(event.artifact ? { artifact: { ...(event.artifact as Artifact), colorScheme } } : {}),
    ...(event.comparison ? { comparison: { ...(event.comparison as Record<string, unknown>), colorScheme } } : {}),
  });
  let stage = 'Preparing browser context';
  let stageStarted = Date.now();
  const report = () => emit({ type: 'target_progress', url, engine, viewportId: viewport.id,
    viewportName: viewport.name, colorScheme, position: target.position, totalTargets: target.totalTargets,
    stage, elapsedSeconds: Math.floor((Date.now() - stageStarted) / 1000) });
  const setStage = (message: string) => { stage = message; stageStarted = Date.now(); report(); };
  const progressTimer = setInterval(report, 2000);
  const presentation = normalizePresentation(job.profile.presentation);
  let blocker: ContentBlocker | undefined;
  const blockingMetrics = () => blocker?.enabled
    ? { ...blocker.metrics, warnings: [...blocker.metrics.warnings],
        sources: blocker.metrics.sources ? { ...blocker.metrics.sources } : {} }
    : undefined;
  try {
    if (runtime.cancelled) return { completed, failed };
    report();
    blocker = await ContentBlocker.load(job);
    const networkPolicy = { allowLocalhost: job.allowLocalhost === true };
    await assertPublicUrl(url, networkPolicy);
    context = await browser.newContext({
      viewport: { width: viewport.width, height: viewport.height },
      deviceScaleFactor: viewport.deviceScaleFactor,
      isMobile: engine === 'firefox' ? false : viewport.mobile,
      hasTouch: viewport.mobile,
      ignoreHTTPSErrors: false,
      colorScheme,
    });
    await installRouting(context, job.profile.blocklist, networkPolicy, blocker);
    page = await context.newPage();
    page.setDefaultNavigationTimeout(job.profile.navigationTimeoutSeconds * 1000);
    page.setDefaultTimeout(job.profile.selectorTimeoutSeconds * 1000);
    emit({ type: 'target_started', url, engine, viewportId: viewport.id, viewportName: viewport.name });
    for (const warning of blocker.metrics.warnings) {
      emit({ type: 'job_warning', message: `Content blocking: ${warning}` });
    }
    setStage('Loading page');
    const response = await page.goto(url, { waitUntil: 'domcontentloaded' });
    if (response && response.status() >= 400) throw new Error(`Navigation returned HTTP ${response.status()}`);
    // Full-page screenshots hide scrollbars, but CSS scrollbar-gutter: stable
    // can leave an empty strip. Reflow at the requested viewport width before
    // preparation instead of cropping pixels or resizing the final image.
    await page.evaluate(() => {
      document.documentElement.style.setProperty('scrollbar-gutter', 'auto', 'important');
      document.documentElement.style.setProperty('scrollbar-width', 'none', 'important');
    });
    // Preparation order per the content-blocking design: load the rules
    // snapshot before navigation (done above), keep the security policy as the
    // highest-priority rule (routing), inject static cosmetic filters into the
    // document and every frame, run consent actions after initial load, after
    // full-page scrolling, and immediately before capture, then apply the
    // user's hide selectors. Scroll locking is only restored by the consent
    // passes themselves and only when a recognized overlay was removed.
    setStage(`Waiting after load (${job.profile.initialDelay} s)`);
    await sleep(job.profile.initialDelay);
    setStage('Applying content filters and handling cookie banners');
    await blocker.applyCosmeticFilters(page);
    await blocker.runConsentPass(page);
    if (job.profile.waitForSelector) {
      setStage(`Waiting for selector: ${job.profile.waitForSelector}`);
      await page.locator(job.profile.waitForSelector).first().waitFor({ state: 'visible' });
    }
    if (job.profile.captureMode === 'fullPage') {
      setStage('Scrolling page to load lazy content');
      await autoScroll(page, job.profile.maxScrollSeconds);
      setStage(`Waiting after scroll (${job.profile.scrollDelay} s)`);
      await sleep(job.profile.scrollDelay);
      await blocker.applyCosmeticFilters(page);
      await blocker.runConsentPass(page);
    }
    setStage('Hiding requested elements and handling remaining banners');
    await hideElements(page, [...job.profile.hideSelectors,
      ...(job.profile.comparisonEnabled ? job.profile.comparisonIgnoreSelectors : [])]);
    await blocker.runConsentPass(page);
    setStage(`Waiting before capture (${job.profile.finalDelay} s)`);
    await sleep(job.profile.finalDelay);
    setStage('Taking screenshot');
    let png = formats.some((format) => format !== 'pdf') ? await screenshotPng(page, job, viewport) : undefined;
    if (png && job.profile.stripWhitespace && job.profile.captureMode === 'fullPage') png = await stripTopWhitespace(png);
    if (png && job.profile.colorScheme === 'both' && !formats.includes('pdf')) {
      setStage('Checking for identical light and dark output');
      imageHash = sha256(png);
      const hasDarkBaseline = job.profile.comparisonEnabled && formats.some(format =>
        job.baselines?.[`${url}|${engine}|${viewport.id}|${job.profile.captureMode}|${format}|dark`]);
      // Exact bytes are deliberately conservative: dynamic content, a failed
      // light capture, existing skipped files, and PDF jobs retain both themes.
      // Keep established dark baselines under review even if themes converge.
      if (colorScheme === 'dark' && lightImageHash === imageHash && !hasDarkBaseline && !runtime.cancelled) {
        const omittedArtifacts = formats.length * (presentation.enabled ? 2 : 1);
        emit({ type: 'target_theme_redundant', url, engine, viewportName: viewport.name, colorScheme,
          position: target.position, omittedArtifacts,
          message: 'Dark output is identical to light; duplicate files and portfolio rendering omitted.' });
        return { completed, failed };
      }
    }
    const name = captureName(job, url, viewport, engine, target.index, colorScheme);
    const targetDirectory = path.join(outputDirectory, ...name.directories);
    if (!inside(path.resolve(job.projectRoot), targetDirectory)) throw new Error('Capture name escapes project root');
    await mkdir(targetDirectory, { recursive: true });
    for (const format of formats) {
      if (runtime.cancelled) break;
      const artifactId = id();
      try {
        setStage(`Writing ${format.toUpperCase()} original`);
        const selection = await allocator.choose(targetDirectory, name.base, format, job.profile.collisionPolicy);
        const relativePath = path.relative(job.projectRoot, selection.absolute);
        // Existing baselines were captured with Playwright's light default.
        const comparisonKey = `${url}|${engine}|${viewport.id}|${job.profile.captureMode}|${format}${colorScheme === 'dark' ? '|dark' : ''}`;
        if (selection.skipped) {
          reusedExistingOutput = true;
          const artifact: Artifact = { id: artifactId, jobId: job.id, url, engine, viewportId: viewport.id,
            viewportName: viewport.name, captureMode: job.profile.captureMode, format, relativePath,
            targetId: target.target.id, targetName: target.target.name,
            targetSetId: target.target.targetSetId, targetSetName: target.target.targetSetName,
            width: viewport.width, height: viewport.height, sha256: '', status: 'skipped',
            variant: 'original', contentBlocking: blockingMetrics(), createdAt: new Date().toISOString() };
          emit({ type: 'artifact_completed', artifact }); completed += 1;
        } else {
          const bytes = format === 'pdf'
            ? Buffer.from(await page.pdf({ format: job.profile.pdfFormat as 'A4', landscape: job.profile.pdfLandscape,
                margin: { top: job.profile.pdfMargin, right: job.profile.pdfMargin,
                          bottom: job.profile.pdfMargin, left: job.profile.pdfMargin }, printBackground: true }))
            : await convertPng(png as Buffer, format, job);
          await atomicWrite(selection.absolute, bytes);
          let width = viewport.width;
          let height = viewport.height;
          if (format !== 'pdf') {
            const metadata = await sharp(bytes).metadata();
            width = metadata.width ?? width;
            height = metadata.height ?? height;
          }
          const artifact: Artifact = { id: artifactId, jobId: job.id, url, finalUrl: page.url(), engine,
            targetId: target.target.id, targetName: target.target.name,
            targetSetId: target.target.targetSetId, targetSetName: target.target.targetSetName,
            viewportId: viewport.id, viewportName: viewport.name, captureMode: job.profile.captureMode, format,
            relativePath, width, height, variant: 'original', contentBlocking: blockingMetrics(),
            sha256: sha256(bytes), status: 'succeeded', createdAt: new Date().toISOString() };
          emit({ type: 'artifact_completed', artifact }); completed += 1;
          if (job.profile.comparisonEnabled && format !== 'pdf') {
            setStage(`Comparing ${format.toUpperCase()} with baseline`);
            try { await compareArtifact(job, artifact, bytes, comparisonKey, emit); }
            catch (comparisonError) {
              emit({ type: 'comparison_completed', comparison: {
                id: id(), jobId: job.id, comparisonKey, baselineArtifactId: job.baselines?.[comparisonKey]?.artifactId ?? '',
                currentArtifactId: artifact.id, status: 'error', mismatchRatio: 0, diffRelativePath: '',
                createdAt: new Date().toISOString(), url, targetId: artifact.targetId ?? '',
                targetName: artifact.targetName ?? '', targetSetId: artifact.targetSetId ?? '',
                targetSetName: artifact.targetSetName ?? '', engine, viewportId: viewport.id,
                viewportName: viewport.name, captureMode: job.profile.captureMode, format,
                analysisWidth: 0, analysisHeight: 0, analysisScale: 1,
                error: comparisonError instanceof Error ? comparisonError.message : String(comparisonError),
              } });
            }
          }
        }
        if (presentation.enabled && format !== 'pdf') {
          setStage(`Rendering ${format.toUpperCase()} portfolio copy`);
          const portfolioId = id();
          try {
            const portfolioSelection = await allocator.choose(targetDirectory, `${name.base}-portfolio`, format,
              job.profile.collisionPolicy);
            const portfolioRelativePath = path.relative(job.projectRoot, portfolioSelection.absolute);
            if (portfolioSelection.skipped) {
              const portfolioArtifact: Artifact = { id: portfolioId, jobId: job.id, url, finalUrl: page.url(), engine,
                targetId: target.target.id, targetName: target.target.name,
                targetSetId: target.target.targetSetId, targetSetName: target.target.targetSetName,
                viewportId: viewport.id, viewportName: viewport.name, captureMode: job.profile.captureMode, format,
                relativePath: portfolioRelativePath, width: 0, height: 0, sha256: '', status: 'skipped',
                variant: 'portfolio', presentation, createdAt: new Date().toISOString() };
              emit({ type: 'artifact_completed', artifact: portfolioArtifact }); completed += 1;
            } else {
              const styled = await renderPresentation(png as Buffer, presentation, viewport, job.profile.captureMode);
              const portfolioBytes = await convertPng(styled.bytes, format, job);
              await atomicWrite(portfolioSelection.absolute, portfolioBytes);
              const effectivePresentation = { ...styled.settings, resolvedFrame: styled.resolvedFrame };
              const portfolioArtifact: Artifact = { id: portfolioId, jobId: job.id, url, finalUrl: page.url(), engine,
                targetId: target.target.id, targetName: target.target.name,
                targetSetId: target.target.targetSetId, targetSetName: target.target.targetSetName,
                viewportId: viewport.id, viewportName: viewport.name, captureMode: job.profile.captureMode, format,
                relativePath: portfolioRelativePath, width: styled.width, height: styled.height,
                sha256: sha256(portfolioBytes), status: 'succeeded', variant: 'portfolio',
                presentation: effectivePresentation, createdAt: new Date().toISOString() };
              emit({ type: 'artifact_completed', artifact: portfolioArtifact }); completed += 1;
            }
          } catch (presentationError) {
            failed += 1;
            const portfolioArtifact: Artifact = { id: portfolioId, jobId: job.id, url, engine,
              targetId: target.target.id, targetName: target.target.name,
              targetSetId: target.target.targetSetId, targetSetName: target.target.targetSetName,
              viewportId: viewport.id, viewportName: viewport.name, captureMode: job.profile.captureMode, format,
              relativePath: '', width: 0, height: 0, sha256: '', status: 'failed', variant: 'portfolio',
              presentation, error: presentationError instanceof Error ? presentationError.message : String(presentationError),
              createdAt: new Date().toISOString() };
            emit({ type: 'artifact_failed', artifact: portfolioArtifact, message: portfolioArtifact.error });
          }
        }
      } catch (artifactError) {
        failed += 1;
        const artifact: Artifact = { id: artifactId, jobId: job.id, url, engine, viewportId: viewport.id,
          targetId: target.target.id, targetName: target.target.name,
          targetSetId: target.target.targetSetId, targetSetName: target.target.targetSetName,
          viewportName: viewport.name, captureMode: job.profile.captureMode, format, relativePath: '',
          width: 0, height: 0, sha256: '', status: 'failed', variant: 'original',
          error: artifactError instanceof Error ? artifactError.message : String(artifactError), createdAt: new Date().toISOString() };
        emit({ type: 'artifact_failed', artifact, message: artifact.error });
        if (presentation.enabled && format !== 'pdf') {
          failed += 1;
          const portfolioArtifact: Artifact = { ...artifact, id: id(), variant: 'portfolio', presentation,
            error: `Portfolio copy not created because the original ${format.toUpperCase()} failed: ${artifact.error}` };
          emit({ type: 'artifact_failed', artifact: portfolioArtifact, message: portfolioArtifact.error });
        }
      }
    }
  } catch (targetError) {
    if (!runtime.cancelled) {
      for (const format of formats) {
        failed += 1;
        const artifact: Artifact = { id: id(), jobId: job.id, url, engine, viewportId: viewport.id,
          targetId: target.target.id, targetName: target.target.name,
          targetSetId: target.target.targetSetId, targetSetName: target.target.targetSetName,
          viewportName: viewport.name, captureMode: job.profile.captureMode, format, relativePath: '',
          width: 0, height: 0, sha256: '', status: 'failed', variant: 'original',
          contentBlocking: blockingMetrics(),
          error: targetError instanceof Error ? targetError.message : String(targetError), createdAt: new Date().toISOString() };
        emit({ type: 'artifact_failed', artifact, message: artifact.error });
        if (presentation.enabled && format !== 'pdf') {
          failed += 1;
          const portfolioArtifact: Artifact = { ...artifact, id: id(), variant: 'portfolio', presentation,
            error: `Portfolio copy not created because the capture failed: ${artifact.error}` };
          emit({ type: 'artifact_failed', artifact: portfolioArtifact, message: portfolioArtifact.error });
        }
      }
    }
  } finally {
    clearInterval(progressTimer);
    await page?.close().catch(() => undefined);
    await context?.close().catch(() => undefined);
    emit({ type: 'target_finished', position: target.position, totalTargets: target.totalTargets,
      url, engine, viewportName: viewport.name, colorScheme });
  }
  return { completed, failed, imageHash: failed === 0 && completed > 0 && !reusedExistingOutput ? imageHash : undefined };
}

export async function runCaptureJob(job: CaptureJob, runtime: JobRuntime, emit: Emit): Promise<void> {
  const root = path.resolve(job.projectRoot);
  const allowedRoot = process.env.CYBERSNAPPER_PROJECT_ROOT
    ? path.resolve(process.env.CYBERSNAPPER_PROJECT_ROOT) : root;
  if (root !== allowedRoot) throw new Error('Job project root does not match the agent-approved project root');
  const day = new Date().toISOString().slice(0, 10);
  const outputDirectory = path.join(root, 'captures', day, safeSegment(job.id));
  if (!inside(root, outputDirectory)) throw new Error('Capture output escapes project root');
  await mkdir(outputDirectory, { recursive: true });
  const initialDisk = await statfs(outputDirectory);
  if (Number(initialDisk.bavail) * Number(initialDisk.bsize) < minimumFreeBytes) {
    throw new Error('At least 256 MiB of free disk space is required to start a capture');
  }
  const targets: CaptureTarget[] = [];
  const snapshots: TargetSnapshot[] = job.targets?.length
    ? job.targets.filter((target) => target.enabled !== false)
    : job.urls.map((url, index) => ({ id: `adhoc-${index + 1}`, name: '', url,
        targetSetId: '', targetSetName: '', enabled: true }));
  for (const engine of job.profile.engines) {
    for (const viewport of job.profile.viewports.filter((candidate) => candidate.enabled)) {
      const formats = job.profile.formats.filter((format) => format !== 'pdf' || engine === 'chromium');
      const schemes: ColorScheme[] = job.profile.colorScheme === 'both' ? ['light', 'dark']
        : [job.profile.colorScheme === 'dark' ? 'dark' : 'light'];
      for (const [index, target] of snapshots.entries()) {
        for (const colorScheme of schemes) targets.push({ index, position: targets.length + 1,
          totalTargets: 0, colorScheme, target, engine, viewport, formats });
      }
    }
  }
  const presentationEnabled = normalizePresentation(job.profile.presentation).enabled;
  let totalArtifacts = targets.reduce((sum, target) => sum + target.formats.reduce(
    (count, format) => count + (presentationEnabled && format !== 'pdf' ? 2 : 1), 0), 0);
  if (totalArtifacts === 0) throw new Error('The job has no enabled capture targets');
  if (totalArtifacts > maximumArtifacts) throw new Error(`A job may create at most ${maximumArtifacts.toLocaleString()} artifacts`);
  for (const target of targets) target.totalTargets = targets.length;
  emit({ type: 'job_started', status: 'running', totalArtifacts, totalTargets: targets.length });
  const browsers = new Map<BrowserEngine, Browser>();
  const proxy = await startFilteringProxy({ allowLocalhost: job.allowLocalhost === true });
  try {
    for (const engine of new Set(targets.map((target) => target.engine))) {
      if (runtime.cancelled) break;
      emit({ type: 'job_stage', stage: `Launching ${engine}`, totalArtifacts });
      const browser = await launchEngine(engine, proxy.url);
      browsers.set(engine, browser);
      runtime.browsers.add(browser);
    }
    const allocator = new OutputPathAllocator();
    let cursor = 0;
    let completed = 0;
    let failed = 0;
    let omittedArtifacts = 0;
    const reportArtifact: Emit = (event) => {
      emit(event);
      if (event.type === 'target_theme_redundant') {
        const omitted = Number(event.omittedArtifacts);
        omittedArtifacts += omitted;
        totalArtifacts -= omitted;
        emit({ type: 'job_progress', completed, failed, totalArtifacts, omittedArtifacts });
      }
      if (event.type === 'artifact_completed' || event.type === 'artifact_failed') {
        if (event.type === 'artifact_completed') completed += 1; else failed += 1;
        emit({ type: 'job_progress', completed, failed, totalArtifacts });
      }
    };
    const worker = async (): Promise<void> => {
      while (!runtime.cancelled) {
        // Keep each light/dark pair on one worker so the dark render can be
        // checked against its successful light counterpart without retaining
        // full-page image buffers or sharing results across different targets.
        const pairSize = job.profile.colorScheme === 'both' ? 2 : 1;
        const index = cursor;
        cursor += pairSize;
        if (index >= targets.length) return;
        const target = targets[index];
        const browser = browsers.get(target.engine);
        if (!browser) return;
        const light = await captureTarget(job, target, browser, outputDirectory, allocator, runtime, reportArtifact);
        if (pairSize === 2 && !runtime.cancelled) {
          await captureTarget(job, targets[index + 1], browser, outputDirectory, allocator, runtime, reportArtifact, light.imageHash);
        }
      }
    };
    const estimatedTargetPixels = job.profile.captureMode === 'viewport'
      ? Math.max(...job.profile.viewports.filter((viewport) => viewport.enabled)
          .map((viewport) => viewport.width * viewport.height * viewport.deviceScaleFactor ** 2))
      : maximumDevicePixels;
    const resourceConcurrency = Math.max(1, Math.floor(maximumConcurrentDevicePixels / Math.max(1, estimatedTargetPixels)));
    const effectiveConcurrency = Math.min(job.profile.concurrency, resourceConcurrency, Math.max(1, targets.length));
    if (effectiveConcurrency < job.profile.concurrency) {
      emit({ type: 'job_warning', message: `Parallel pages reduced to ${effectiveConcurrency} for the image-memory safety budget` });
    }
    await Promise.all(Array.from({ length: effectiveConcurrency }, worker));
    if (runtime.cancelled) emit({ type: 'job_cancelled', status: 'cancelled', completed, failed });
    else if (failed === 0) emit({ type: 'job_succeeded', status: 'succeeded', completed, failed,
      ...(omittedArtifacts ? { message: `Capture complete; ${omittedArtifacts} identical dark output files omitted.`, omittedArtifacts } : {}) });
    else if (completed > 0) emit({ type: 'job_partial', status: 'partial', completed, failed, message: `${failed} artifacts failed` });
    else emit({ type: 'job_failed', status: 'failed', completed, failed, message: 'Every capture failed' });
  } finally {
    await Promise.all([...browsers.values()].map((browser) => browser.close().catch(() => undefined)));
    runtime.browsers.clear();
    await proxy.close();
  }
}
