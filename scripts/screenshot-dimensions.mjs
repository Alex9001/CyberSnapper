// Documentation captures may be rendered at native 1x or 2x display scale.
export function isAppScreenshot({ width, height }) {
  return [1, 2].some((scale) => width === 1280 * scale && height === 800 * scale);
}

export function isPresentationScreenshot({ width = 0, height = 0 }) {
  return [1, 2].some((scale) => width >= 700 * scale && height >= 400 * scale && height <= 560 * scale);
}
