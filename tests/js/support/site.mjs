// Shared helpers for the Node test files in tests/js (node --test).
//
// The modules under test are always the current static/ sources, staged with
// corec's WASI host exactly where dist/ has it (scripts/build.mjs
// VENDOR_FILES), so a stale dist/ can never be tested by accident. The engine
// is the production reactor: $MS_WASM, else build/wasm/<REACTOR_FILE>
// (rebuilt by both `scripts/build.mjs reactor` and `dist`), else
// dist/<REACTOR_FILE>. Real-WASM tests never skip: without the reactor they
// fail with build instructions (`npm run test:js:unit` runs only the tests
// that need no WASM).

import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { Worker as ThreadWorker } from 'node:worker_threads';
import { REACTOR_FILE, VENDOR_FILES } from '../../../scripts/build.mjs';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const STAGE = join(ROOT, 'build', 'js-test', `site-${process.pid}`);

let staged = null;

// file:// URL of the staged site directory (created once per process).
export function siteUrl() {
  if (!staged) {
    rmSync(STAGE, { recursive: true, force: true });
    mkdirSync(STAGE, { recursive: true });
    for (const name of readdirSync(join(ROOT, 'static'))) {
      cpSync(join(ROOT, 'static', name), join(STAGE, name), { recursive: true });
    }
    for (const [target, source] of Object.entries(VENDOR_FILES)) {
      mkdirSync(dirname(join(STAGE, target)), { recursive: true });
      cpSync(join(ROOT, source), join(STAGE, target));
    }
    process.on('exit', () => rmSync(STAGE, { recursive: true, force: true }));
    staged = pathToFileURL(STAGE + '/');
  }
  return staged;
}

export function importSite(name) {
  return import(new URL(name, siteUrl()).href);
}

export function wasmPath() {
  if (process.env.MS_WASM) return existsSync(process.env.MS_WASM) ? process.env.MS_WASM : null;
  const candidates = [join(ROOT, 'build', 'wasm', REACTOR_FILE), join(ROOT, 'dist', REACTOR_FILE)];
  return candidates.find((path) => existsSync(path)) || null;
}

// The production reactor's path, or an error explaining how to build it.
export function requireWasm() {
  const path = wasmPath();
  if (path) return path;
  const missing = process.env.MS_WASM
    ? `MS_WASM=${process.env.MS_WASM} does not exist.`
    : `No production reactor: neither build/wasm/${REACTOR_FILE} nor dist/${REACTOR_FILE} exists.`;
  throw new Error(`${missing}\nThese tests run against the real engine. Build it first:\n` +
    '  pixi run -e js build-dist        (or: pixi run -e wasm node scripts/build.mjs reactor)\n' +
    'or set MS_WASM to a built minesweeper.wasm. Tests that need no WASM: npm run test:js:unit');
}

let compiled = null;

export function engineModule() {
  if (!compiled) compiled = WebAssembly.compile(readFileSync(requireWasm()));
  return compiled;
}

/*
 * A Web Worker over node:worker_threads for the real probability-worker.js:
 * the thread runs worker-thread.mjs, which provides the worker-global API
 * the module uses (self.addEventListener, self.postMessage, self.close) and
 * then imports it. `faults` are applied inside the thread before the import:
 * clockThrows makes performance.now throw (a broken host import, seen by a
 * solver that reads the clock); solveTraps makes ms_solve_observation throw
 * a RuntimeError, as a trap escaping the export would.
 */
export function nodeWorkerFactory(faults = {}) {
  const created = [];
  const factory = (url) => {
    const worker = new NodeWebWorker(url, faults);
    created.push(worker);
    return worker;
  };
  factory.created = created;
  return factory;
}

class NodeWebWorker {
  constructor(url, faults) {
    this.onmessage = null;
    this.onerror = null;
    this.onmessageerror = null;
    this.terminated = false;
    this.posted = [];
    this.postedBytes = [];
    this.thread = new ThreadWorker(new URL('./worker-thread.mjs', import.meta.url), {
      workerData: { url: String(url), faults: faults }
    });
    this.thread.unref();
    this.thread.on('message', (data) => {
      if (!this.terminated && this.onmessage) this.onmessage({ data: data });
    });
    this.thread.on('messageerror', () => {
      if (!this.terminated && this.onmessageerror) this.onmessageerror({});
    });
    this.thread.on('error', (error) => {
      if (!this.terminated && this.onerror) this.onerror({ message: error.message });
    });
  }

  postMessage(message, transfer) {
    this.posted.push(message);
    this.postedBytes.push(message.observation instanceof ArrayBuffer ? message.observation.byteLength : null);
    this.thread.postMessage(message, transfer);
  }

  terminate() {
    this.terminated = true;
    this.thread.terminate();
  }
}

// An in-memory Web Worker double for SolverHost tests: records what the host
// posts and lets the test answer, fail or crash it.
export class FakeWorker {
  constructor(url) {
    this.url = url;
    this.handlers = { message: null, error: null };
    this.lastHandlers = { message: null, error: null };
    this.onmessageerror = null;
    this.posted = [];
    this.transfers = [];
    this.terminated = false;
  }

  get onmessage() {
    return this.handlers.message;
  }

  set onmessage(handler) {
    this.handlers.message = handler;
    if (handler) this.lastHandlers.message = handler;
  }

  get onerror() {
    return this.handlers.error;
  }

  set onerror(handler) {
    this.handlers.error = handler;
    if (handler) this.lastHandlers.error = handler;
  }

  postMessage(message, transfer) {
    if (this.terminated) throw new Error('posted to a terminated worker');
    this.posted.push(message);
    this.transfers.push(transfer || []);
  }

  terminate() {
    this.terminated = true;
  }

  // Delivers a message to the current handler, as the browser would.
  reply(data) {
    if (this.onmessage) this.onmessage({ data: data });
  }

  crash(message) {
    if (this.onerror) this.onerror({ message: message });
  }

  // An event that was already queued when the host discarded this worker:
  // dispatched to the handler installed before, which must ignore it.
  replyStale(data) {
    if (this.lastHandlers.message) this.lastHandlers.message({ data: data });
  }

  crashStale(message) {
    if (this.lastHandlers.error) this.lastHandlers.error({ message: message });
  }

  last(type) {
    return [...this.posted].reverse().find((message) => message.type === type);
  }
}

export function fakeWorkerFactory() {
  const created = [];
  const factory = (url) => {
    const worker = new FakeWorker(url);
    created.push(worker);
    return worker;
  };
  factory.created = created;
  return factory;
}

// Resolves after pending promise callbacks have run.
export function flush() {
  return new Promise((resolve) => setImmediate(resolve));
}

export async function waitFor(predicate, timeoutMs = 5000) {
  const end = Date.now() + timeoutMs;
  while (!predicate()) {
    if (Date.now() > end) throw new Error('timed out waiting for a condition');
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}
