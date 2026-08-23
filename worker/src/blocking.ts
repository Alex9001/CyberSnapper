import crypto from 'node:crypto';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { FiltersEngine, Request } from '@ghostery/adblocker';
import type { Page } from 'playwright';
import type { ContentBlocking, ContentBlockingMetrics, RulesetSourceVersion, StructuredAction } from './protocol.js';

// Content ruleset snapshots are written by the agent into
// `.cybersnapper/rulesets/<digest>.json` inside the project. The file carries a
// canonical payload string so its SHA-256 digest can be verified without
// depending on JSON key ordering across Qt and JavaScript.

export interface RulesetSnapshotPayload {
  generatedAt: string;
  subscriptions: Record<string, Record<string, unknown>>;
  rulesText: string[];
  actions: StructuredAction[];
}

export interface RulesetSnapshot {
  format: number;
  digest: string;
  payload: RulesetSnapshotPayload;
}

export interface NormalizedRules {
  lines: string[];
  actions: StructuredAction[];
  unsupported: number;
}

const maximumSnapshotBytes = 32 * 1024 * 1024;
const maximumSnapshotRules = 200_000;
const maximumCustomActions = 200;
const maximumActionDelayMs = 5000;
const maximumSelectorLength = 200;

// Options that rewrite requests or responses are never safe to apply from
// community lists: they can redirect, inject headers, mutate query strings, or
// filter response bodies. Network filters must only use plain matching types.
const allowedNetworkOptions = new Set([
  'script', 'image', 'stylesheet', 'object', 'xmlhttprequest', 'subdocument',
  'document', 'ping', 'media', 'font', 'websocket', 'other', 'popup', 'all',
  'third-party', 'first-party', 'important', 'badfilter',
  'strict-third-party', 'strict-first-party',
]);

function isUnsafeNetworkFilter(line: string): boolean {
  const dollar = line.lastIndexOf('$');
  if (dollar < 0) return false;
  const optionsText = line.slice(dollar + 1);
  if (!/^[\w~,.=!|\-\s]*$/.test(optionsText)) return true;
  for (const rawOption of optionsText.toLowerCase().split(',')) {
    const option = rawOption.trim();
    if (!option) continue;
    const negated = option.startsWith('~') ? option.slice(1) : option;
    const [name, ...rest] = negated.split('=');
    if (name === 'domain' || name === 'from' || name === 'to' || name === 'method') {
      if (rest.length === 0) return true;
      continue;
    }
    if (rest.length > 0) return true;
    if (!allowedNetworkOptions.has(name)) return true;
  }
  return false;
}

interface CosmeticMarker { marker: string; index: number }

const unsupportedCosmeticMarkers = ['#@%#', '#$?#', '#$#', '#%#', '#@$', '#$'];
const supportedCosmeticMarkers = ['#@?#', '#@#', '##'];
const allCosmeticMarkers = [...unsupportedCosmeticMarkers, ...supportedCosmeticMarkers];

function findCosmeticMarker(line: string): CosmeticMarker | null {
  for (let index = line.indexOf('#'); index >= 0; index = line.indexOf('#', index + 1)) {
    for (const marker of allCosmeticMarkers) {
      if (line.startsWith(marker, index)) return { marker, index };
    }
  }
  return null;
}

const selectorCharsetPattern = /^[A-Za-z0-9_\-.>#\[\]=|^$:*~\s,"']+$/;
const domainPattern = /^[A-Za-z0-9*_.\-]+$/;

function splitTopLevelArgs(text: string): string[] {
  const parts: string[] = [];
  let current = '';
  let quote = '';
  for (const character of text) {
    if (quote) {
      if (character === quote) quote = '';
      current += character;
      continue;
    }
    if (character === '"' || character === "'") { quote = character; current += character; continue; }
    if (character === ',') { parts.push(current); current = ''; continue; }
    current += character;
  }
  parts.push(current);
  return parts.map((part) => part.trim()).filter((part) => part.length > 0);
}

function validActionSelector(selector: string): boolean {
  if (selector.length === 0 || selector.length > maximumSelectorLength) return false;
  if (!selectorCharsetPattern.test(selector) || selector.includes('..')) return false;
  // Clicking an anchor navigates instead of dismissing an overlay; reject any
  // selector that targets an `a` element.
  if (/(^|[\s>+~,])a([\s.,#:>[+~]|$)/i.test(selector)) return false;
  return true;
}

// Only `trusted-click-element` is recognized, and only from curated official
// lists. The grammar is deliberately bounded: no parentheses, no escapes, a
// small selector count and length, and a small bounded final delay.
export function parseTrustedClickAction(domainsText: string, body: string): StructuredAction | null {
  if (!body.startsWith('+js(') || !body.endsWith(')')) return null;
  const args = splitTopLevelArgs(body.slice(4, -1));
  if (args.length < 2 || args.length > 6) return null;
  if (args[0] !== 'trusted-click-element') return null;
  if (!domainsText) return null;
  const domains: string[] = [];
  for (const rawDomain of domainsText.split(',')) {
    const domain = rawDomain.trim().toLowerCase();
    if (!domain || domain.startsWith('~') || domain.startsWith('*') && domain !== '*') return null;
    if (!domainPattern.test(domain)) return null;
    domains.push(domain);
  }
  if (domains.length === 0 || domains.length > 8) return null;
  let delayMs = 0;
  const selectors: string[] = [];
  for (const [index, arg] of args.slice(1).entries()) {
    const isLast = index === args.length - 2;
    if (isLast && /^\d{1,5}$/.test(arg)) { delayMs = Math.min(Number(arg), maximumActionDelayMs); continue; }
    if (selectors.length >= 4) return null;
    if (!validActionSelector(arg)) return null;
    selectors.push(arg);
  }
  if (selectors.length === 0) return null;
  return { domains, selector: selectors.join(', '), action: 'click', delayMs };
}

// Normalizes community filter text into a safe subset. Scriptlets other than
// trusted-click-element, HTML rewrites, style injections, preprocessor
// directives, and response/redirect options are rejected and counted.
export function normalizeRulesText(text: string): NormalizedRules {
  const lines: string[] = [];
  const actions: StructuredAction[] = [];
  let unsupported = 0;
  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    // Preprocessor directives (`!#if`) are directives, not comments, and are
    // never safe to honor from community lists.
    if (line.startsWith('!#')) { unsupported += 1; continue; }
    if (!line || line.startsWith('!') || line.startsWith('[')) continue;
    const marker = findCosmeticMarker(line);
    if (!marker) {
      // Network filter patterns must be plain patterns or /regex/ filters.
      // Anything containing shell-ish characters is treated as unsupported.
      const looksLikeRegex = /^@?\|?\/.+\/$/.test(line.split('$')[0]);
      if (!looksLikeRegex && /[`\\\n\r<>{};]/.test(line)) { unsupported += 1; continue; }
      if (isUnsafeNetworkFilter(line)) { unsupported += 1; continue; }
      lines.push(line);
      continue;
    }
    const { marker: token, index } = marker;
    const domainsText = line.slice(0, index);
    const body = line.slice(index + token.length);
    if (token === '##' && body.startsWith('+js(')) {
      const action = parseTrustedClickAction(domainsText, body);
      if (action && actions.length < maximumCustomActions) actions.push(action);
      else unsupported += 1;
      continue;
    }
    if (!supportedCosmeticMarkers.includes(token)) { unsupported += 1; continue; }
    if (body.startsWith('^')) { unsupported += 1; continue; }
    if (/:style\(/i.test(body) || /:remove\(/i.test(body) || /:remove\(\)/i.test(body)) {
      unsupported += 1;
      continue;
    }
    if (body.length > 4096) { unsupported += 1; continue; }
    lines.push(line);
  }
  return { lines, actions, unsupported };
}

export function sha256Hex(text: string): string {
  return crypto.createHash('sha256').update(text, 'utf8').digest('hex');
}

function parsePayload(payloadText: string): RulesetSnapshotPayload {
  const payload = JSON.parse(payloadText) as RulesetSnapshotPayload;
  if (!payload || typeof payload !== 'object') throw new Error('Invalid rules snapshot payload');
  if (!Array.isArray(payload.rulesText) || payload.rulesText.length > maximumSnapshotRules
      || payload.rulesText.some((line) => typeof line !== 'string')) {
    throw new Error('Invalid rules snapshot rules');
  }
  if (!Array.isArray(payload.actions) || payload.actions.length > maximumCustomActions) {
    throw new Error('Invalid rules snapshot actions');
  }
  for (const action of payload.actions) {
    const domainsValid = Array.isArray(action?.domains) && action.domains.length > 0
      && action.domains.length <= 8
      && action.domains.every((domain) => typeof domain === 'string' && domainPattern.test(domain));
    if (!action || typeof action !== 'object' || !domainsValid
        || !validActionSelector(action.selector ?? '')
        || (action.action !== 'click' && action.action !== 'hide')
        || typeof action.delayMs !== 'number' || action.delayMs < 0
        || action.delayMs > maximumActionDelayMs) {
      throw new Error('Invalid structured action in rules snapshot');
    }
  }
  return payload;
}

export function verifySnapshotText(text: string): RulesetSnapshot {
  if (text.length > maximumSnapshotBytes) throw new Error('Rules snapshot exceeds 32 MiB');
  const document = JSON.parse(text) as { format?: number; digest?: string; payload?: string };
  if (document.format !== 1 || typeof document.digest !== 'string' || typeof document.payload !== 'string') {
    throw new Error('Invalid rules snapshot format');
  }
  if (!/^[0-9a-f]{64}$/.test(document.digest)) throw new Error('Invalid rules snapshot digest');
  if (sha256Hex(document.payload) !== document.digest) throw new Error('Rules snapshot digest mismatch');
  return { format: document.format, digest: document.digest, payload: parsePayload(document.payload) };
}

function inside(root: string, candidate: string): boolean {
  const relative = path.relative(root, candidate);
  return relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
}

export async function loadRulesSnapshot(projectRoot: string, relativePath: string, digest: string):
    Promise<RulesetSnapshot> {
  const resolved = path.resolve(projectRoot, relativePath);
  if (!inside(path.resolve(projectRoot), resolved)) throw new Error('Rules snapshot path escapes project');
  if (path.basename(resolved) !== `${digest}.json`) throw new Error('Rules snapshot digest does not match its path');
  const text = await readFile(resolved, 'utf8');
  const snapshot = verifySnapshotText(text);
  if (snapshot.digest !== digest) throw new Error('Rules snapshot digest mismatch');
  return snapshot;
}

export function mapResourceType(resourceType: string):
    'script' | 'stylesheet' | 'image' | 'media' | 'font' | 'websocket'
    | 'sub_frame' | 'xmlhttprequest' | 'ping' | 'other' {
  switch (resourceType) {
    case 'script': return 'script';
    case 'stylesheet': return 'stylesheet';
    case 'image': return 'image';
    case 'media': return 'media';
    case 'font': return 'font';
    case 'xhr': case 'fetch': case 'eventsource': return 'xmlhttprequest';
    case 'websocket': return 'websocket';
    case 'ping': return 'ping';
    // Only iframe documents reach the matcher; the main document is excluded.
    case 'document': return 'sub_frame';
    default: return 'other';
  }
}

export function hostMatches(host: string, domain: string): boolean {
  const normalizedHost = host.toLowerCase().replace(/\.$/, '');
  const normalizedDomain = domain.toLowerCase().replace(/\.$/, '').replace(/^\*\./, '');
  if (!normalizedDomain) return false;
  return normalizedHost === normalizedDomain || normalizedHost.endsWith(`.${normalizedDomain}`);
}

function hostMatchesAny(host: string, domains: string[]): boolean {
  return domains.some((domain) => hostMatches(host, domain));
}

function hostnameOf(urlText: string): string {
  try { return new URL(urlText).hostname.toLowerCase(); } catch { return ''; }
}

// Runs inside a frame. Collects bounded DOM information that the adblocker
// engine needs to select generic class/id/href cosmetic filters.
function collectDomInfoInPage(): { classes: string[]; ids: string[]; hrefs: string[] } {
  const classes = new Set<string>();
  const ids = new Set<string>();
  const hrefs = new Set<string>();
  let scanned = 0;
  for (const element of document.querySelectorAll('*')) {
    if (scanned >= 20_000 || classes.size + ids.size + hrefs.size >= 10_000) break;
    scanned += 1;
    for (const className of element.classList) {
      if (classes.size < 5000) classes.add(className);
    }
    if (element.id && ids.size < 5000) ids.add(element.id);
    const reference = element.getAttribute('href') ?? element.getAttribute('src');
    if (reference && hrefs.size < 5000) hrefs.add(reference);
  }
  return { classes: [...classes], ids: [...ids], hrefs: [...hrefs] };
}

export function emptyMetrics(): ContentBlockingMetrics {
  return {
    blockedSubresources: 0, cosmeticRulesApplied: 0, consentActionsAttempted: 0,
    consentActionsSucceeded: 0, unsupportedRules: 0, staleCache: false, warnings: [],
  };
}

// Profiles from agents older than 2.3 may lack the contentBlocking object;
// treat that as blocking off rather than crashing the capture.
function normalizeSettings(settings?: Partial<ContentBlocking>): ContentBlocking {
  return {
    enabled: settings?.enabled === true,
    consentStrategy: settings?.consentStrategy === 'dismiss' ? 'dismiss' : 'rejectThenDismiss',
    subscriptionIds: Array.isArray(settings?.subscriptionIds) ? [...settings!.subscriptionIds!] : [],
    customRulesetIds: Array.isArray(settings?.customRulesetIds) ? [...settings!.customRulesetIds!] : [],
    disabledDomains: Array.isArray(settings?.disabledDomains) ? [...settings!.disabledDomains!] : [],
    versionPolicy: settings?.versionPolicy === 'pinned' ? 'pinned' : 'latest',
  };
}

// Runs inside the page. Finds consent/cookie containers by consent-related
// text or attributes, clicks a reject (or dismiss) control inside the
// identified container only, and reports what happened. Normal application
// dialogs without consent markers are never touched.
//
// Everything this function needs is defined inside its body: Playwright
// serializes only the function source into the page, so module-scope
// constants would not exist there.
function consentPassInPage(strategy: string): {
  attempted: number; succeeded: number; removed: number; locked: boolean;
} {
  const consentContainerSelector = [
    '[id*="cookie" i]', '[class*="cookie" i]', '[name*="cookie" i]', '[aria-label*="cookie" i]',
    '[id*="consent" i]', '[class*="consent" i]', '[aria-label*="consent" i]',
    '[data-testid*="cookie" i]', '[data-testid*="consent" i]',
    '[id*="gdpr" i]', '[class*="gdpr" i]',
    '[id*="onetrust" i]', '[class*="onetrust" i]',
    '[id*="usercentrics" i]', '[class*="usercentrics" i]',
    '[id*="cookiebot" i]', '[class*="cookiebot" i]',
    '[id*="qc-cmp" i]', '[class*="qc-cmp" i]', '[id*="truste" i]',
  ].join(',');
  const rejectPatterns = /^(reject\s+(all|everything|optional)|reject|decline\s+(all|everything|optional)|decline|deny\s+all|deny|refuse(\s+all)?|opt\s?out(\s+of\s+all)?|continue\s+without\s+accepting|use\s+necessary\s+cookies?\s+only|only\s+necessary|necessary\s+cookies?\s+only|essential\s+cookies?\s+only|do\s+not\s+sell(\/share)?(\s+my)?(\s+(personal\s+)?(info|information|data))?)/i;
  const dismissPatterns = /^(accept\s+(all|everything|and\s+(close|continue))?|allow\s+(all|everything|necessary(\s+cookies?)?)|agree(\s+and\s+(close|continue))?|ok(,?\s*got\s*it\.?)?|okay|got\s+it|i\s+understand|yes,?\s*(i\s+)?(accept|agree|understand)|sounds\s+good|that's\s+ok|understood)/i;
  const report = { attempted: 0, succeeded: 0, removed: 0, locked: false };
  const deadline = Date.now() + 2500;
  const root = document.documentElement;
  const body = document.body;
  const isLocked = (element: HTMLElement | null): boolean => {
    if (!element) return false;
    const style = getComputedStyle(element);
    return style.overflow.includes('hidden') || style.overflowY.includes('hidden');
  };
  report.locked = isLocked(root) || isLocked(body);
  const visible = (element: Element): boolean => {
    const rect = element.getBoundingClientRect();
    if (rect.width < 40 || rect.height < 20) return false;
    const style = getComputedStyle(element);
    return style.display !== 'none' && style.visibility !== 'hidden' && style.opacity !== '0'
      && element.getAttribute('aria-hidden') !== 'true';
  };
  const containers: HTMLElement[] = [];
  for (const element of document.querySelectorAll<HTMLElement>(consentContainerSelector)) {
    if (containers.length >= 8) break;
    if (!visible(element)) continue;
    // Require consent/cookie/privacy text or attributes: the attribute
    // selector already matched; also require a clickable control inside.
    if (!element.querySelector('button, [role="button"], input[type="button"], input[type="submit"], a[href]')) continue;
    containers.push(element);
  }
  const buttonLabel = (element: HTMLElement): string => {
    const text = (element.innerText || element.textContent || '').trim().replace(/\s+/g, ' ');
    if (text && text.length <= 60) return text;
    const label = element.getAttribute('aria-label') || element.getAttribute('title')
      || (element as HTMLInputElement).value || '';
    return label.trim();
  };
  const clickFirst = (container: HTMLElement, pattern: RegExp): boolean => {
    for (const element of container.querySelectorAll<HTMLElement>(
      'button, [role="button"], input[type="button"], input[type="submit"]')) {
      if (Date.now() > deadline) return false;
      if (!visible(element)) continue;
      const label = buttonLabel(element);
      if (!label || label.length > 60 || !pattern.test(label)) continue;
      report.attempted += 1;
      try { element.click(); } catch { continue; }
      report.succeeded += 1;
      return true;
    }
    return false;
  };
  for (const container of containers) {
    if (Date.now() > deadline) break;
    const rejectFirst = strategy !== 'dismiss';
    const clicked = rejectFirst
      ? clickFirst(container, rejectPatterns) || clickFirst(container, dismissPatterns)
      : clickFirst(container, dismissPatterns);
    if (!clicked) continue;
    const stillThere = container.isConnected && visible(container);
    if (!stillThere) report.removed += 1;
  }
  if (report.removed > 0 && report.locked) {
    for (const element of [root, body]) {
      if (element) element.style.setProperty('overflow', 'auto', 'important');
    }
  }
  return report;
}

// First-party PageSpeed handler: identify the Google cookie banner through its
// cookie-policy link or consent text, click the known dismiss control (with an
// accessible-name fallback), and only hide the scoped banner if clicking
// failed. Scroll locking is restored only when this handler removed the banner.
function pageSpeedPassInPage(): { clicked: boolean; hid: boolean; locked: boolean } {
  const report = { clicked: false, hid: false, locked: false };
  const deadline = Date.now() + 1500;
  const visible = (element: Element): boolean => {
    if (!element.isConnected) return false;
    const rect = element.getBoundingClientRect();
    const style = getComputedStyle(element);
    return rect.width > 0 && rect.height > 0 && style.display !== 'none' && style.visibility !== 'hidden';
  };
  const locked = [document.documentElement, document.body].some((element) => element
    && (getComputedStyle(element).overflow.includes('hidden')
        || getComputedStyle(element).position === 'fixed'));
  report.locked = locked;
  let banner: HTMLElement | null = null;
  const link = document.querySelector<HTMLAnchorElement>('a[href*="technologies/cookies"]');
  if (link) {
    let candidate: HTMLElement | null = link.parentElement;
    for (let depth = 0; candidate && depth < 8; depth += 1) {
      const style = getComputedStyle(candidate);
      if (style.position === 'fixed' || style.position === 'sticky') banner = candidate;
      candidate = candidate.parentElement;
    }
  }
  if (!banner) {
    for (const element of document.querySelectorAll<HTMLElement>(
        '[id*="cookie" i], [class*="cookie" i], [aria-label*="cookie" i]')) {
      if (visible(element) && element.querySelector('button')) { banner = element; break; }
    }
  }
  const button = document.querySelector<HTMLElement>('button[jsname="yfXuWd"]')
    ?? (banner ? Array.from(banner.querySelectorAll<HTMLElement>('button, [role="button"]'))
      .find((element) => /^(ok,?\s*got\s*it\.?|got\s+it)$/i.test((element.innerText || '').trim())) : null);
  if (button && visible(button)) {
    report.clicked = true;
    try { button.click(); } catch { report.clicked = false; }
  }
  const bannerGone = !banner || !visible(banner);
  if (report.clicked && bannerGone && locked) {
    for (const element of [document.documentElement, document.body]) {
      if (element) element.style.setProperty('overflow', 'auto', 'important');
    }
  } else if (!bannerGone && banner && Date.now() <= deadline) {
    banner.style.setProperty('display', 'none', 'important');
    report.hid = true;
    if (locked) {
      for (const element of [document.documentElement, document.body]) {
        if (element) element.style.setProperty('overflow', 'auto', 'important');
      }
    }
  }
  return report;
}

export class ContentBlocker {
  readonly metrics: ContentBlockingMetrics;
  private readonly settings: ContentBlocking;
  private engine: FiltersEngine | null = null;
  private actions: StructuredAction[] = [];
  private disabledDomains: string[] = [];
  private snapshot: RulesetSnapshot | null = null;

  private constructor(settings: ContentBlocking) {
    this.settings = settings;
    this.metrics = emptyMetrics();
    this.disabledDomains = settings.disabledDomains.map((domain) => domain.trim().toLowerCase())
      .filter(Boolean);
  }

  get enabled(): boolean { return this.settings.enabled; }
  get hasEngine(): boolean { return this.engine !== null; }

  static inert(settings: ContentBlocking): ContentBlocker {
    return new ContentBlocker({ ...settings, enabled: false });
  }

  static async load(job: {
    projectRoot: string;
    profile: { contentBlocking?: Partial<ContentBlocking> };
    ruleset?: { digest?: string; relativePath?: string;
      sources?: Record<string, RulesetSourceVersion>; warnings?: string[] };
  }): Promise<ContentBlocker> {
    const blocker = new ContentBlocker(normalizeSettings(job.profile.contentBlocking));
    if (!blocker.enabled) return blocker;
    blocker.metrics.sources = job.ruleset?.sources
      ? Object.fromEntries(Object.entries(job.ruleset.sources)
          .map(([key, value]) => [key, { ...value }])) as Record<string, RulesetSourceVersion>
      : {};
    for (const warning of job.ruleset?.warnings ?? []) blocker.metrics.warnings.push(String(warning));
    if (!job.ruleset?.digest || !job.ruleset?.relativePath) {
      blocker.metrics.staleCache = true;
      blocker.metrics.warnings.push('No community rules snapshot was available; using built-in consent handling only.');
      return blocker;
    }
    try {
      blocker.snapshot = await loadRulesSnapshot(job.projectRoot, job.ruleset.relativePath, job.ruleset.digest);
    } catch (error) {
      blocker.metrics.staleCache = true;
      blocker.metrics.warnings.push(`Rules snapshot could not be loaded: ${
        error instanceof Error ? error.message : String(error)}`);
      return blocker;
    }
    blocker.metrics.rulesetDigest = blocker.snapshot.digest;
    blocker.metrics.unsupportedRules = normalizeRulesText(blocker.snapshot.payload.rulesText.join('\n')).unsupported;
    try {
      blocker.engine = FiltersEngine.parse(blocker.snapshot.payload.rulesText.join('\n'));
    } catch (error) {
      blocker.engine = null;
      blocker.metrics.warnings.push(`Rules snapshot could not be parsed: ${
        error instanceof Error ? error.message : String(error)}`);
    }
    blocker.actions = blocker.snapshot.payload.actions;
    return blocker;
  }

  /** True only for subresources; the main document is never community-blocked. */
  blocksSubresource(url: string, resourceType: string): boolean {
    if (!this.enabled || !this.engine) return false;
    if (this.disabledDomains.length && hostMatchesAny(hostnameOf(url), this.disabledDomains)) return false;
    try {
      return this.engine.match(Request.fromRawDetails({ url, type: mapResourceType(resourceType) })).match;
    } catch { return false; }
  }

  cosmeticStyles(url: string, dom: { classes: string[]; ids: string[]; hrefs: string[] } = {
    classes: [], ids: [], hrefs: [],
  }): string | null {
    if (!this.enabled || !this.engine) return null;
    const hostname = hostnameOf(url);
    if (!hostname || !/^https?:/i.test(url)) return null;
    if (this.disabledDomains.length && hostMatchesAny(hostname, this.disabledDomains)) return null;
    try {
      const { styles, active } = this.engine.getCosmeticsFilters({
        url, hostname, domain: hostname,
        classes: dom.classes, ids: dom.ids, hrefs: dom.hrefs,
        getRulesFromDOM: true, getRulesFromHostname: true,
      });
      return active && styles ? styles : null;
    } catch { return null; }
  }

  async applyCosmeticFilters(page: Page): Promise<void> {
    if (!this.enabled || !this.snapshot) return;
    const digest = this.snapshot.digest;
    for (const frame of page.frames()) {
      try {
        const alreadyApplied = await frame.evaluate(
          (parts: string[]) =>
            (window as unknown as Record<string, unknown>)[parts[0]] === parts[1],
          ['__cybersnapperCosmeticsDigest', digest],
        );
        if (alreadyApplied) continue;
        const dom = await frame.evaluate(collectDomInfoInPage);
        const styles = this.cosmeticStyles(frame.url(), dom);
        if (!styles) continue;
        await frame.addStyleTag({ content: styles });
        await frame.evaluate((value: string): boolean => {
          (window as unknown as Record<string, unknown>)['__cybersnapperCosmeticsDigest'] = value;
          return true;
        }, digest);
        this.metrics.cosmeticRulesApplied += (styles.match(/\{/g) ?? []).length;
      } catch { /* Frames may navigate or be gone; cosmetic injection is best effort. */ }
    }
  }

  private async runCustomActions(page: Page): Promise<void> {
    if (!this.enabled) return;
    const host = hostnameOf(page.url());
    if (!host || (this.disabledDomains.length && hostMatchesAny(host, this.disabledDomains))) return;
    const applicable = this.actions.filter((action) => hostMatchesAny(host, action.domains));
    let delayBudget = 5000;
    for (const action of applicable) {
      const delay = Math.max(0, Math.min(action.delayMs, delayBudget));
      delayBudget -= delay;
      if (delay > 0) await new Promise((resolve) => setTimeout(resolve, delay));
      this.metrics.consentActionsAttempted += 1;
      try {
        if (action.action === 'hide') {
          await page.locator(action.selector).first().evaluateAll((elements) => {
            for (const element of elements) {
              if (element instanceof HTMLElement) element.style.setProperty('display', 'none', 'important');
            }
          });
        } else {
          await page.locator(action.selector).first().click({ timeout: 1500, trial: false });
        }
        this.metrics.consentActionsSucceeded += 1;
      } catch { /* A failed custom action must never fail the capture. */ }
    }
  }

  async runConsentPass(page: Page, strategy?: string): Promise<void> {
    if (!this.enabled) return;
    const host = hostnameOf(page.url());
    if (!host || (this.disabledDomains.length && hostMatchesAny(host, this.disabledDomains))) return;
    const effectiveStrategy = strategy ?? this.settings.consentStrategy;
    try {
      if (host === 'pagespeed.web.dev' || host.endsWith('.pagespeed.web.dev')) {
        const report = await page.evaluate(pageSpeedPassInPage);
        if (report.clicked || report.hid) {
          this.metrics.consentActionsAttempted += 1;
          this.metrics.consentActionsSucceeded += 1;
        }
        return;
      }
      const report = await page.evaluate(consentPassInPage, effectiveStrategy);
      this.metrics.consentActionsAttempted += report.attempted;
      this.metrics.consentActionsSucceeded += report.succeeded;
    } catch { /* Consent handling is best effort and never fails a capture. */ }
    await this.runCustomActions(page);
  }
}
