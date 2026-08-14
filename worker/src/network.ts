import dns from 'node:dns/promises';
import net from 'node:net';

const checkedHosts = new Map<string, Promise<void>>();

function privateIPv4(address: string): boolean {
  const parts = address.split('.').map(Number);
  if (parts.length !== 4 || parts.some((part) => !Number.isInteger(part) || part < 0 || part > 255)) return true;
  const [a, b] = parts;
  return a === 0 || a === 10 || a === 127 ||
    (a === 100 && b >= 64 && b <= 127) ||
    (a === 169 && b === 254) ||
    (a === 172 && b >= 16 && b <= 31) ||
    (a === 192 && b === 0) ||
    (a === 192 && b === 168) ||
    (a === 192 && b === 88) ||
    (a === 198 && (b === 18 || b === 19)) ||
    (a === 198 && b === 51 && parts[2] === 100) ||
    (a === 203 && b === 0 && parts[2] === 113) ||
    a >= 224;
}

function privateIp(address: string): boolean {
  if (net.isIPv4(address)) return privateIPv4(address);
  if (!net.isIPv6(address)) return true;
  const normalized = address.toLowerCase().split('%')[0];
  if (normalized.startsWith('::ffff:')) return privateIPv4(normalized.slice(7));
  return normalized === '::' || normalized === '::1' || normalized.startsWith('fc') ||
         normalized.startsWith('fd') || normalized.startsWith('ff') || /^fe[89a-f]/.test(normalized) ||
         normalized.startsWith('2001:db8') || normalized.startsWith('2001:2:');
}

async function checkHost(hostname: string): Promise<void> {
  let host = hostname.toLowerCase().replace(/\.$/, '');
  if (host.startsWith('[') && host.endsWith(']')) host = host.slice(1, -1);
  if (!host) {
    throw new Error(`Private or local host is not allowed: ${hostname}`);
  }
  if (net.isIP(host)) {
    if (privateIp(host)) throw new Error(`Private network address is not allowed: ${hostname}`);
    return;
  }
  if (host === 'localhost' || host.endsWith('.localhost') || host.endsWith('.local') || !host.includes('.')) {
    throw new Error(`Private or local host is not allowed: ${hostname}`);
  }
  const addresses = await dns.lookup(host, { all: true, verbatim: true });
  if (addresses.length === 0 || addresses.some(({ address }) => privateIp(address))) {
    throw new Error(`Host resolves to a private or unavailable address: ${hostname}`);
  }
}

export function assertPublicUrl(urlText: string): Promise<void> {
  let url: URL;
  try { url = new URL(urlText); } catch { return Promise.reject(new Error(`Invalid URL: ${urlText}`)); }
  if (!['http:', 'https:'].includes(url.protocol)) return Promise.reject(new Error(`Unsupported URL protocol: ${url.protocol}`));
  if (url.username || url.password) return Promise.reject(new Error('URLs containing credentials are not allowed'));
  let pending = checkedHosts.get(url.hostname);
  if (!pending) {
    pending = checkHost(url.hostname);
    checkedHosts.set(url.hostname, pending);
  }
  return pending;
}
