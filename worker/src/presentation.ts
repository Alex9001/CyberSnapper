import sharp from 'sharp';
import type {
  CaptureMode, PresentationAspect, PresentationFrame, PresentationPadding, PresentationScene,
  PresentationSettings, PresentationShadow, Viewport,
} from './protocol.js';

const maximumDimension = 16_384;
const maximumPixels = 64_000_000;

const scenes: PresentationScene[] = ['clean', 'aurora', 'sunset', 'midnight', 'graphite', 'customSolid'];
const frames: PresentationFrame[] = [
  'auto', 'none', 'roundedCard', 'lightBrowser', 'darkBrowser',
  'lightTablet', 'darkTablet', 'lightPhone', 'darkPhone',
];
const aspects: PresentationAspect[] = ['auto', '16:9', '4:3', 'square'];
const paddings: PresentationPadding[] = ['compact', 'balanced', 'generous'];
const shadows: PresentationShadow[] = ['none', 'soft', 'strong'];

export const defaultPresentation: PresentationSettings = {
  enabled: false,
  scene: 'aurora',
  frame: 'auto',
  aspect: 'auto',
  padding: 'balanced',
  shadow: 'soft',
  solidColor: '#0B1220',
};

export function normalizePresentation(value: Partial<PresentationSettings> | null | undefined): PresentationSettings {
  const color = typeof value?.solidColor === 'string' && /^#[0-9a-f]{6}$/i.test(value.solidColor.trim())
    ? value.solidColor.trim().toUpperCase() : defaultPresentation.solidColor;
  return {
    enabled: value?.enabled === true,
    scene: scenes.includes(value?.scene as PresentationScene) ? value?.scene as PresentationScene : defaultPresentation.scene,
    frame: frames.includes(value?.frame as PresentationFrame) ? value?.frame as PresentationFrame : defaultPresentation.frame,
    aspect: aspects.includes(value?.aspect as PresentationAspect) ? value?.aspect as PresentationAspect : defaultPresentation.aspect,
    padding: paddings.includes(value?.padding as PresentationPadding) ? value?.padding as PresentationPadding : defaultPresentation.padding,
    shadow: shadows.includes(value?.shadow as PresentationShadow) ? value?.shadow as PresentationShadow : defaultPresentation.shadow,
    solidColor: color,
  };
}

type ResolvedFrame = Exclude<PresentationFrame, 'auto'>;

export interface PresentationPlan {
  canvasWidth: number;
  canvasHeight: number;
  screenshotWidth: number;
  screenshotHeight: number;
  screenshotX: number;
  screenshotY: number;
  frameX: number;
  frameY: number;
  frameWidth: number;
  frameHeight: number;
  frameSide: number;
  frameTop: number;
  frameBottom: number;
  cornerRadius: number;
  resolvedFrame: ResolvedFrame;
}

function bounded(value: number, minimum: number, maximum: number): number {
  return Math.max(minimum, Math.min(maximum, Math.round(value)));
}

type PresentationViewport = Pick<Viewport, 'mobile' | 'width' | 'height'>;

function resolveFrame(frame: PresentationFrame, viewport: PresentationViewport,
                      captureMode: CaptureMode): ResolvedFrame {
  if (frame !== 'auto') return frame;
  if (captureMode === 'fullPage' || captureMode === 'element') return 'roundedCard';
  if (!viewport.mobile) return 'lightBrowser';
  return Math.min(viewport.width, viewport.height) >= 600 ? 'darkTablet' : 'darkPhone';
}

function ratioFor(aspect: PresentationAspect): number | undefined {
  if (aspect === '16:9') return 16 / 9;
  if (aspect === '4:3') return 4 / 3;
  if (aspect === 'square') return 1;
  return undefined;
}

export function planPresentation(width: number, height: number, settingsValue: Partial<PresentationSettings>,
                                 viewport: PresentationViewport, captureMode: CaptureMode): PresentationPlan {
  if (!Number.isFinite(width) || !Number.isFinite(height) || width < 1 || height < 1) {
    throw new Error('Presentation source has invalid dimensions');
  }
  const settings = normalizePresentation(settingsValue);
  const resolvedFrame = resolveFrame(settings.frame, viewport, captureMode);
  const shortSide = Math.min(width, height);
  const padding = settings.padding === 'compact' ? bounded(shortSide * 0.04, 28, 96)
    : settings.padding === 'generous' ? bounded(shortSide * 0.13, 64, 240)
      : bounded(shortSide * 0.08, 48, 160);

  let frameSide = 0;
  let frameTop = 0;
  let frameBottom = 0;
  let cornerRadius = 0;
  if (resolvedFrame === 'roundedCard') {
    frameSide = 3; frameTop = 3; frameBottom = 3; cornerRadius = bounded(shortSide * 0.025, 12, 30);
  } else if (resolvedFrame.endsWith('Browser')) {
    frameSide = bounded(width * 0.002, 2, 5);
    frameTop = bounded(width * 0.042, 42, 76);
    frameBottom = frameSide;
    cornerRadius = bounded(width * 0.012, 12, 24);
  } else if (resolvedFrame.endsWith('Tablet')) {
    frameSide = bounded(width * 0.025, 14, 32);
    frameTop = bounded(width * 0.035, 18, 40);
    frameBottom = frameTop;
    cornerRadius = bounded(width * 0.055, 24, 52);
  } else if (resolvedFrame.endsWith('Phone')) {
    frameSide = bounded(width * 0.058, 18, 42);
    frameTop = bounded(width * 0.09, 28, 58);
    frameBottom = bounded(width * 0.075, 24, 52);
    cornerRadius = bounded(width * 0.085, 26, 58);
  }

  const framedWidth = width + frameSide * 2;
  const framedHeight = height + frameTop + frameBottom;
  let canvasWidth = framedWidth + padding * 2;
  let canvasHeight = framedHeight + padding * 2;
  const ratio = ratioFor(settings.aspect);
  if (ratio) {
    if (canvasWidth / canvasHeight < ratio) canvasWidth = Math.ceil(canvasHeight * ratio);
    else canvasHeight = Math.ceil(canvasWidth / ratio);
  }
  const scale = Math.min(1, maximumDimension / canvasWidth, maximumDimension / canvasHeight,
    Math.sqrt(maximumPixels / (canvasWidth * canvasHeight)));
  canvasWidth = Math.max(1, Math.floor(canvasWidth * scale));
  canvasHeight = Math.max(1, Math.floor(canvasHeight * scale));
  const scaledWidth = Math.max(1, Math.floor(width * scale));
  const scaledHeight = Math.max(1, Math.floor(height * scale));
  frameSide = Math.max(resolvedFrame === 'none' ? 0 : 1, Math.floor(frameSide * scale));
  frameTop = Math.max(resolvedFrame === 'none' ? 0 : 1, Math.floor(frameTop * scale));
  frameBottom = Math.max(resolvedFrame === 'none' ? 0 : 1, Math.floor(frameBottom * scale));
  cornerRadius = Math.max(resolvedFrame === 'none' ? 0 : 2, Math.floor(cornerRadius * scale));
  const frameWidth = scaledWidth + frameSide * 2;
  const frameHeight = scaledHeight + frameTop + frameBottom;
  const frameX = Math.floor((canvasWidth - frameWidth) / 2);
  const frameY = Math.floor((canvasHeight - frameHeight) / 2);
  return {
    canvasWidth, canvasHeight, screenshotWidth: scaledWidth, screenshotHeight: scaledHeight,
    screenshotX: frameX + frameSide, screenshotY: frameY + frameTop,
    frameX, frameY, frameWidth, frameHeight, frameSide, frameTop, frameBottom, cornerRadius,
    resolvedFrame,
  };
}

function sceneDefinitions(scene: PresentationScene, solidColor: string): { definitions: string; fill: string; accents: string } {
  if (scene === 'clean') return {
    definitions: '<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#FFFFFF"/><stop offset="1" stop-color="#E8EEF7"/></linearGradient>',
    fill: 'url(#bg)',
    accents: '<circle cx="12%" cy="15%" r="24%" fill="#DCE9FF" opacity=".56"/><circle cx="90%" cy="90%" r="30%" fill="#EDE4FF" opacity=".45"/>',
  };
  if (scene === 'aurora') return {
    definitions: '<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#16113A"/><stop offset=".5" stop-color="#4C1D95"/><stop offset="1" stop-color="#075985"/></linearGradient><radialGradient id="a"><stop stop-color="#5EEAD4" stop-opacity=".82"/><stop offset="1" stop-color="#5EEAD4" stop-opacity="0"/></radialGradient><radialGradient id="b"><stop stop-color="#F0ABFC" stop-opacity=".72"/><stop offset="1" stop-color="#F0ABFC" stop-opacity="0"/></radialGradient>',
    fill: 'url(#bg)',
    accents: '<ellipse cx="78%" cy="18%" rx="42%" ry="50%" fill="url(#a)"/><ellipse cx="12%" cy="92%" rx="48%" ry="58%" fill="url(#b)"/>',
  };
  if (scene === 'sunset') return {
    definitions: '<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#7C2D92"/><stop offset=".52" stop-color="#E11D48"/><stop offset="1" stop-color="#FB923C"/></linearGradient><radialGradient id="sun"><stop stop-color="#FDE68A" stop-opacity=".9"/><stop offset="1" stop-color="#FDE68A" stop-opacity="0"/></radialGradient>',
    fill: 'url(#bg)', accents: '<ellipse cx="80%" cy="5%" rx="45%" ry="62%" fill="url(#sun)"/>',
  };
  if (scene === 'midnight') return {
    definitions: '<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#020617"/><stop offset=".55" stop-color="#111827"/><stop offset="1" stop-color="#172554"/></linearGradient><radialGradient id="glow"><stop stop-color="#2563EB" stop-opacity=".5"/><stop offset="1" stop-color="#2563EB" stop-opacity="0"/></radialGradient>',
    fill: 'url(#bg)', accents: '<ellipse cx="100%" cy="0" rx="55%" ry="70%" fill="url(#glow)"/>',
  };
  if (scene === 'graphite') return {
    definitions: '<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#0F1115"/><stop offset="1" stop-color="#3F4652"/></linearGradient><pattern id="grid" width="48" height="48" patternUnits="userSpaceOnUse"><path d="M48 0H0V48" fill="none" stroke="#FFFFFF" stroke-opacity=".045"/></pattern>',
    fill: 'url(#bg)', accents: '<rect width="100%" height="100%" fill="url(#grid)"/>',
  };
  return { definitions: '', fill: solidColor, accents: '' };
}

function renderSvg(plan: PresentationPlan, settings: PresentationSettings): Buffer {
  const { definitions, fill, accents } = sceneDefinitions(settings.scene, settings.solidColor);
  const dark = plan.resolvedFrame.startsWith('dark');
  const phone = plan.resolvedFrame.endsWith('Phone');
  const tablet = plan.resolvedFrame.endsWith('Tablet');
  const browser = plan.resolvedFrame.endsWith('Browser');
  const frameFill = dark ? '#111827' : '#F8FAFC';
  const border = dark ? '#334155' : '#CBD5E1';
  const shadow = settings.shadow === 'none' ? '' : settings.shadow === 'strong'
    ? '<filter id="shadow" x="-30%" y="-30%" width="160%" height="180%"><feDropShadow dx="0" dy="18" stdDeviation="22" flood-color="#000000" flood-opacity=".48"/></filter>'
    : '<filter id="shadow" x="-25%" y="-25%" width="150%" height="170%"><feDropShadow dx="0" dy="10" stdDeviation="14" flood-color="#000000" flood-opacity=".28"/></filter>';
  const filter = settings.shadow === 'none' ? '' : ' filter="url(#shadow)"';
  let frame = '';
  if (plan.resolvedFrame !== 'none') {
    frame += `<rect x="${plan.frameX}" y="${plan.frameY}" width="${plan.frameWidth}" height="${plan.frameHeight}" rx="${plan.cornerRadius}" fill="${frameFill}" stroke="${border}" stroke-width="${Math.max(1, plan.frameSide)}"${filter}/>`;
  }
  if (browser) {
    const cy = plan.frameY + plan.frameTop / 2;
    const radius = Math.max(2, plan.frameTop * 0.09);
    const start = plan.frameX + plan.frameTop * 0.42;
    frame += `<circle cx="${start}" cy="${cy}" r="${radius}" fill="#FB7185"/><circle cx="${start + radius * 2.8}" cy="${cy}" r="${radius}" fill="#FBBF24"/><circle cx="${start + radius * 5.6}" cy="${cy}" r="${radius}" fill="#34D399"/>`;
    const addressX = start + radius * 9;
    const addressWidth = Math.max(10, plan.frameWidth - (addressX - plan.frameX) - plan.frameTop * 0.35);
    frame += `<rect x="${addressX}" y="${cy - radius * 1.45}" width="${addressWidth}" height="${radius * 2.9}" rx="${radius * 1.45}" fill="${dark ? '#1E293B' : '#E2E8F0'}"/>`;
  } else if (phone) {
    const pillWidth = Math.min(plan.frameWidth * 0.32, plan.frameTop * 2.4);
    const pillHeight = Math.max(3, plan.frameTop * 0.22);
    frame += `<rect x="${plan.frameX + (plan.frameWidth - pillWidth) / 2}" y="${plan.frameY + (plan.frameTop - pillHeight) / 2}" width="${pillWidth}" height="${pillHeight}" rx="${pillHeight / 2}" fill="${dark ? '#020617' : '#94A3B8'}"/>`;
  } else if (tablet) {
    const cameraRadius = Math.max(2, Math.min(5, plan.frameTop * 0.12));
    const indicatorWidth = Math.min(plan.frameWidth * 0.14, plan.frameBottom * 2.2);
    const indicatorHeight = Math.max(2, plan.frameBottom * 0.11);
    const hardwareFill = dark ? '#020617' : '#94A3B8';
    frame += `<circle cx="${plan.frameX + plan.frameWidth / 2}" cy="${plan.frameY + plan.frameTop / 2}" r="${cameraRadius}" fill="${hardwareFill}"/>`;
    frame += `<rect x="${plan.frameX + (plan.frameWidth - indicatorWidth) / 2}" y="${plan.frameY + plan.frameHeight - (plan.frameBottom + indicatorHeight) / 2}" width="${indicatorWidth}" height="${indicatorHeight}" rx="${indicatorHeight / 2}" fill="${hardwareFill}"/>`;
  }
  return Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="${plan.canvasWidth}" height="${plan.canvasHeight}" viewBox="0 0 ${plan.canvasWidth} ${plan.canvasHeight}"><defs>${definitions}${shadow}</defs><rect width="100%" height="100%" fill="${fill}"/>${accents}${frame}</svg>`);
}

export interface PresentationResult {
  bytes: Buffer;
  width: number;
  height: number;
  settings: PresentationSettings;
  resolvedFrame: ResolvedFrame;
}

export async function renderPresentation(png: Buffer, settingsValue: Partial<PresentationSettings>,
                                         viewport: PresentationViewport,
                                         captureMode: CaptureMode): Promise<PresentationResult> {
  const metadata = await sharp(png).metadata();
  if (!metadata.width || !metadata.height) throw new Error('Could not read screenshot dimensions for presentation styling');
  const settings = normalizePresentation(settingsValue);
  const plan = planPresentation(metadata.width, metadata.height, settings, viewport, captureMode);
  let screenshot = sharp(png).ensureAlpha().resize(plan.screenshotWidth, plan.screenshotHeight, { fit: 'fill' });
  const screenshotRadius = plan.resolvedFrame === 'none' ? 0
    : plan.resolvedFrame.endsWith('Phone') || plan.resolvedFrame.endsWith('Tablet')
      ? Math.max(2, plan.cornerRadius - plan.frameSide)
      : plan.resolvedFrame === 'roundedCard' ? plan.cornerRadius : Math.max(2, Math.floor(plan.cornerRadius * 0.45));
  if (screenshotRadius > 0) {
    const mask = Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="${plan.screenshotWidth}" height="${plan.screenshotHeight}"><rect width="100%" height="100%" rx="${screenshotRadius}" fill="#fff"/></svg>`);
    screenshot = screenshot.composite([{ input: mask, blend: 'dest-in' }]);
  }
  const screenshotBytes = await screenshot.png().toBuffer();
  const bytes = await sharp(renderSvg(plan, settings))
    .composite([{ input: screenshotBytes, left: plan.screenshotX, top: plan.screenshotY }])
    .png()
    .toBuffer();
  return { bytes, width: plan.canvasWidth, height: plan.canvasHeight, settings, resolvedFrame: plan.resolvedFrame };
}
