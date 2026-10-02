// Runs inside a node:worker_threads thread for nodeWorkerFactory()
// (site.mjs): provides the dedicated-worker globals probability-worker.js
// uses, applies requested faults, then imports the real worker module.
import { parentPort, workerData } from 'node:worker_threads';

const listeners = { message: [], messageerror: [] };

globalThis.self = globalThis;
globalThis.addEventListener = (type, listener) => {
  if (!listeners[type]) throw new Error('worker-thread: unsupported event ' + type);
  listeners[type].push(listener);
};
globalThis.postMessage = (message, transfer) => parentPort.postMessage(message, transfer);
globalThis.close = () => {
  parentPort.close();
  process.exit(0);
};

parentPort.on('message', (data) => {
  for (const listener of listeners.message) listener({ data: data });
});
parentPort.on('messageerror', () => {
  for (const listener of listeners.messageerror) listener({});
});

if (workerData.faults.clockThrows) {
  performance.now = () => {
    throw new Error('injected clock fault');
  };
}

// The solver or planner export fails as a trap would (RuntimeError escaping
// the call), independently of what the engine does internally.
if (workerData.faults.solveTraps || workerData.faults.planTraps) {
  const instantiate = WebAssembly.instantiate;
  const trap = () => {
    throw new WebAssembly.RuntimeError('unreachable');
  };
  WebAssembly.instantiate = async (...args) => {
    const instance = await instantiate(...args);
    const exports = Object.assign({}, instance.exports,
      workerData.faults.solveTraps ? { ms_solve_observation: trap } : {},
      workerData.faults.planTraps ? { ms_plan_observation: trap } : {});
    return { exports: exports };
  };
}

await import(workerData.url);
