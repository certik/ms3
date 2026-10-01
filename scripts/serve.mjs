#!/usr/bin/env node
// Minimal static file server on Node's built-in http module (no Python):
//
//   node scripts/serve.mjs [DIR] [--port N] [--host H] [--base /PREFIX/]
//
// Serves DIR (default dist/) read-only at http://H:N/PREFIX/ (defaults
// 127.0.0.1, 8000, /), so the static site can be checked at the root or
// under a project subpath. GET/HEAD only, no directory listings, no dotfiles,
// no paths outside DIR; directories serve their index.html.

import { createReadStream, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, resolve, sep } from 'node:path';

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.map': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.txt': 'text/plain; charset=utf-8',
};

function usage(message) {
  console.error(`serve.mjs: ${message}\n` +
    'usage: node scripts/serve.mjs [DIR] [--port N] [--host H] [--base /PREFIX/]');
  process.exit(2);
}

function parseArgs(argv) {
  const options = { dir: 'dist', port: 8000, host: '127.0.0.1', base: '/' };
  let dirSeen = false;
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const value = () => (i + 1 < argv.length ? argv[++i] : usage(`${arg} needs a value`));
    if (arg === '--port') {
      const text = value();
      options.port = /^\d{1,5}$/.test(text) && Number(text) <= 65535 ? Number(text) : usage('bad port');
    } else if (arg === '--host') {
      options.host = value();
    } else if (arg === '--base') {
      options.base = value();
    } else if (!arg.startsWith('-') && !dirSeen) {
      options.dir = arg;
      dirSeen = true;
    } else {
      usage(`unexpected argument ${arg}`);
    }
  }
  if (!options.base.startsWith('/')) options.base = `/${options.base}`;
  if (!options.base.endsWith('/')) options.base += '/';
  return options;
}

const options = parseArgs(process.argv.slice(2));
const root = resolve(options.dir);
try {
  if (!statSync(root).isDirectory()) usage(`${root} is not a directory`);
} catch {
  usage(`${root} does not exist (build the static site first)`);
}

function send(res, status, text, headers = {}) {
  res.writeHead(status, {
    'Content-Type': 'text/plain; charset=utf-8',
    'Content-Length': Buffer.byteLength(text),
    'Cache-Control': 'no-cache',
    'X-Content-Type-Options': 'nosniff',
    ...headers,
  });
  res.end(res.req.method === 'HEAD' ? undefined : text);
}

// The file for a request path under the mount, or null.
function locate(pathname) {
  let decoded;
  try {
    decoded = decodeURIComponent(pathname);
  } catch {
    return { status: 400 };
  }
  if (decoded.includes('\0')) return { status: 400 };
  const parts = decoded.slice(options.base.length).split('/').filter(Boolean);
  if (parts.some((part) => part === '..' || part.startsWith('.'))) return { status: 404 };
  let file = resolve(join(root, ...parts));
  if (file !== root && !file.startsWith(root + sep)) return { status: 404 };
  try {
    let stat = statSync(file);
    if (stat.isDirectory()) {
      if (!decoded.endsWith('/')) return { redirect: `${pathname}/` };
      file = join(file, 'index.html');
      stat = statSync(file);
    }
    return stat.isFile() ? { file, size: stat.size } : { status: 404 };
  } catch {
    return { status: 404 };
  }
}

const server = createServer((req, res) => {
  let url;
  try {
    url = new URL(req.url, 'http://localhost');
  } catch {
    send(res, 400, 'Bad request\n');
    console.log(`${req.method} ${req.url} 400`);
    return;
  }
  let status = 200;
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    status = 405;
    send(res, status, 'Method not allowed\n', { Allow: 'GET, HEAD' });
  } else if (url.pathname === options.base.slice(0, -1) && options.base !== '/') {
    status = 301;
    send(res, status, 'Moved\n', { Location: options.base + url.search });
  } else if (!url.pathname.startsWith(options.base)) {
    status = 404;
    send(res, status, 'Not found\n');
  } else {
    const found = locate(url.pathname);
    if (found.redirect) {
      status = 301;
      send(res, status, 'Moved\n', { Location: found.redirect + url.search });
    } else if (!found.file) {
      status = found.status;
      send(res, status, status === 400 ? 'Bad request\n' : 'Not found\n');
    } else {
      res.writeHead(200, {
        'Content-Type': TYPES[extname(found.file).toLowerCase()] || 'application/octet-stream',
        'Content-Length': found.size,
        'Cache-Control': 'no-cache',
        'X-Content-Type-Options': 'nosniff',
      });
      if (req.method === 'HEAD') res.end();
      else createReadStream(found.file).on('error', () => res.destroy()).pipe(res);
    }
  }
  console.log(`${req.method} ${req.url} ${status}`);
});

server.on('error', (error) => {
  console.error(`serve.mjs: cannot listen on ${options.host}:${options.port}: ${error.message}`);
  process.exit(1);
});

server.listen(options.port, options.host, () => {
  const { port } = server.address();
  const host = options.host.includes(':') ? `[${options.host}]` : options.host;
  console.log(`Serving ${root} at http://${host}:${port}${options.base} (Ctrl+C to stop)`);
});
