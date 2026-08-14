import dns from 'node:dns/promises';
import http, { type IncomingMessage } from 'node:http';
import net from 'node:net';
import { once } from 'node:events';

export interface NetworkPolicy { allowLocalhost?: boolean; }

export interface ResolvedTarget {
  address: string;
  family: 4 | 6;
  hostname: string;
  local: boolean;
}

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

function loopbackIp(address: string): boolean {
  if (net.isIPv4(address)) return address.startsWith('127.');
  const normalized = address.toLowerCase().split('%')[0];
  return normalized === '::1' || normalized.startsWith('::ffff:127.');
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

export async function resolveAllowedHost(hostname: string, policy: NetworkPolicy = {}): Promise<ResolvedTarget> {
  let host = hostname.toLowerCase().replace(/\.$/, '');
  if (host.startsWith('[') && host.endsWith(']')) host = host.slice(1, -1);
  if (!host) throw new Error(`Private or local host is not allowed: ${hostname}`);
  const localName = host === 'localhost' || host.endsWith('.localhost');
  if (localName && !policy.allowLocalhost) {
    throw new Error(`Private or local host is not allowed: ${hostname}`);
  }
  if (host.endsWith('.local') || (!host.includes('.') && !localName && !net.isIP(host))) {
    throw new Error(`Private or local host is not allowed: ${hostname}`);
  }
  const addresses = net.isIP(host)
    ? [{ address: host, family: net.isIPv4(host) ? 4 as const : 6 as const }]
    : await dns.lookup(host, { all: true, verbatim: true });
  if (addresses.length === 0) throw new Error(`Host is unavailable: ${hostname}`);
  const hasPrivate = addresses.some(({ address }) => privateIp(address));
  const allLoopback = addresses.every(({ address }) => loopbackIp(address));
  if (hasPrivate && !(policy.allowLocalhost && allLoopback && (localName || net.isIP(host)))) {
    throw new Error(`Private network address is not allowed: ${hostname}`);
  }
  // Reject mixed public/private answers. A new lookup is performed for every proxy
  // connection, and the browser is connected to this exact checked address.
  if (hasPrivate && !allLoopback) throw new Error(`Host resolves to mixed or private addresses: ${hostname}`);
  const selected = addresses[0];
  return { address: selected.address, family: selected.family as 4 | 6,
           hostname: host, local: allLoopback };
}

export async function assertPublicUrl(urlText: string, policy: NetworkPolicy = {}): Promise<void> {
  let url: URL;
  try { url = new URL(urlText); } catch { throw new Error(`Invalid URL: ${urlText}`); }
  if (!['http:', 'https:'].includes(url.protocol)) throw new Error(`Unsupported URL protocol: ${url.protocol}`);
  if (url.username || url.password) throw new Error('URLs containing credentials are not allowed');
  await resolveAllowedHost(url.hostname, policy);
}

function connectDestination(hostname: string, port: number, policy: NetworkPolicy): Promise<net.Socket> {
  return resolveAllowedHost(hostname, policy).then((target) => new Promise((resolve, reject) => {
    const socket = net.connect({ host: target.address, port, family: target.family });
    socket.once('connect', () => resolve(socket));
    socket.once('error', reject);
  }));
}

function splitAuthority(authority: string, fallbackPort: number): { hostname: string; port: number } {
  try {
    const parsed = new URL(`http://${authority}`);
    return { hostname: parsed.hostname, port: Number(parsed.port || fallbackPort) };
  } catch { return { hostname: '', port: fallbackPort }; }
}

function rejectSocket(socket: net.Socket, status = '403 Forbidden'): void {
  socket.end(`HTTP/1.1 ${status}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
}

export async function startFilteringProxy(policy: NetworkPolicy = {}): Promise<{ url: string; close: () => Promise<void> }> {
  const server = http.createServer(async (request, response) => {
    try {
      const targetUrl = new URL(request.url ?? '');
      if (targetUrl.protocol !== 'http:') throw new Error('Unsupported proxy request');
      if (targetUrl.username || targetUrl.password) throw new Error('Credentials are not allowed');
      const target = await resolveAllowedHost(targetUrl.hostname, policy);
      const headers: http.OutgoingHttpHeaders = { ...request.headers, host: targetUrl.host };
      delete headers['proxy-authorization'];
      delete headers['proxy-connection'];
      const upstream = http.request({ host: target.address, family: target.family,
        port: Number(targetUrl.port || 80), method: request.method, path: `${targetUrl.pathname}${targetUrl.search}`,
        headers });
      upstream.on('response', (upstreamResponse: IncomingMessage) => {
        response.writeHead(upstreamResponse.statusCode ?? 502, upstreamResponse.statusMessage, upstreamResponse.headers);
        upstreamResponse.pipe(response);
      });
      upstream.on('error', () => { if (!response.headersSent) response.writeHead(502); response.end(); });
      request.pipe(upstream);
    } catch { response.writeHead(403, { Connection: 'close' }); response.end(); }
  });
  server.on('connect', async (request, client, head) => {
    const clientSocket = client as net.Socket;
    const { hostname, port } = splitAuthority(request.url ?? '', 443);
    if (!hostname || port < 1 || port > 65535) return rejectSocket(clientSocket, '400 Bad Request');
    try {
      const upstream = await connectDestination(hostname, port, policy);
      clientSocket.write('HTTP/1.1 200 Connection Established\r\n\r\n');
      if (head.length) upstream.write(head);
      upstream.pipe(clientSocket);
      clientSocket.pipe(upstream);
      upstream.on('error', () => clientSocket.destroy());
    } catch { rejectSocket(clientSocket); }
  });
  server.on('upgrade', async (request, socket, head) => {
    const client = socket as net.Socket;
    try {
      const targetUrl = new URL(request.url ?? '');
      if (!['http:', 'ws:'].includes(targetUrl.protocol) || targetUrl.username || targetUrl.password) {
        return rejectSocket(client);
      }
      const upstream = await connectDestination(targetUrl.hostname, Number(targetUrl.port || 80), policy);
      const headerLines: string[] = [];
      for (const [name, value] of Object.entries(request.headers)) {
        if (value === undefined || name === 'proxy-authorization' || name === 'proxy-connection') continue;
        headerLines.push(`${name}: ${Array.isArray(value) ? value.join(', ') : value}`);
      }
      upstream.write(`${request.method ?? 'GET'} ${targetUrl.pathname}${targetUrl.search} HTTP/${request.httpVersion}\r\n${headerLines.join('\r\n')}\r\n\r\n`);
      if (head.length) upstream.write(head);
      upstream.pipe(client); client.pipe(upstream);
      upstream.on('error', () => client.destroy());
    } catch { rejectSocket(client); }
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('Could not start filtering proxy');
  return { url: `http://127.0.0.1:${address.port}`,
    close: async () => { server.close(); await once(server, 'close').catch(() => undefined); } };
}
