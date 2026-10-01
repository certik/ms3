#!/usr/bin/env node
// WebAssembly checks (run through pixi, e.g. `pixi run -e js check-wasm`):
//
//   1. builds the test runner build/wasm/tests-<id>/ms_tests.wasm (corec
//      _start test runner; --suite as in build.mjs, default all) and the
//      test-only reactor, and audits both modules' imports and exports
//      against explicit allowlists (build.mjs auditWasmModule): corec's WASI
//      imports plus the documented ms_host boundary;
//   2. proves the gates are strict: an undefined symbol and a compiler-rt
//      helper (__multi3 from a 128-bit multiply) must both fail to link, the
//      source policy scan must reject system headers, __int128 and calls to
//      C library memory functions, and the export declaration parser, the
//      static-site reference check, the dist/ release file allowlist and the
//      Windows dumpbin audits (memory adapter recursion, memory helpers
//      imported from a DLL) must reject what they exist to reject;
//   3. instantiates the smoke reactor with corec's platform/js/wasi.js and
//      checks one-time initialization, aligned budgeted buffers, memory
//      growth and view refresh, the injected clock, ABI buffers marshalled
//      from JS, known-answer RNG/hash values shared with the native tests,
//      and proc_exit propagation;
//   4. without --suite (or with --suite all) builds the production reactor
//      build/wasm/minesweeper.wasm, audits it against the exports declared
//      in c/wasm_api.{h,c} (the move advisor's included), and instantiates
//      it: ms_abi_version() before ms_init(), ms_init() exactly once, and
//      the advisor's exports refusing bad buffers before touching them,
//      planning a tiny position and releasing their workspace. It fails,
//      listing the missing files, until every engine source exists. A
//      --suite subset run says explicitly that it did not check the
//      production reactor.
//
// The smoke reactor, link/policy probes and site fixtures are built in a
// private scratch directory, build/check-wasm-XXXXXX/, removed afterwards, so
// concurrent runs (e.g. different --suite subsets) never share a file.

import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, relative } from 'node:path';
import { makeWasi, ProcExit } from '../third_party/corec/platform/js/wasi.js';
import {
  BuildError, COREC_WASM_EXPORTS, HOST_IMPORTS, PLANNER_EXPORTS, REACTOR_FILE, ROOT, STATIC_FILES, VENDOR_FILES,
  abiVersion, auditWasmModule, buildReactor, buildSmoke, buildWasm, checkMemoryAdapters,
  checkSiteReferences, checkSourcePolicy, checkWindowsImports, parseCoffSymbols, parseDumpbinImports,
  parseExportDeclarations, parseSuites, tryLinkWasm, verifyDist,
} from './build.mjs';

const SMOKE_FUNCTIONS = [...COREC_WASM_EXPORTS, 'smoke_abi_version', 'smoke_init', 'smoke_alloc',
  'smoke_free', 'smoke_capacity', 'smoke_live_bytes', 'smoke_hash', 'smoke_rng', 'smoke_neighbors',
  'smoke_clock_ms', 'smoke_obs_validate', 'smoke_result_init', 'smoke_result_validate',
  'smoke_view_validate', 'smoke_limits_validate', 'smoke_exit'];

const MS_OK = 0;
const MS_ERR_INVALID_OBSERVATION = 8;
const MS_ERR_INVALID_LIMITS = 9;
const MS_ERR_INVALID_RESULT = 10;
const MS_ERR_INVALID_BUFFER = 11;
const MS_ERR_STALE_REVISION = 32;
const MS_ERR_INTERNAL = 65;

let checks = 0;

function check(condition, message) {
  if (!condition) throw new Error(`check failed: ${message}`);
  checks++;
}

function same(actual, expected, message) {
  check(actual === expected, `${message}: got ${String(actual)}, expected ${String(expected)}`);
}

function audited(path, options) {
  const module = auditWasmModule(path, options);
  check(true, `audit of ${path}`);
  return module;
}

// Runs `action`, expecting a BuildError whose message contains `expected`.
function rejects(action, expected, what) {
  let message = '';
  try {
    action();
  } catch (error) {
    if (!(error instanceof BuildError)) throw error;
    message = error.message;
  }
  check(message.includes(expected), `${what} must be rejected with "${expected}", got "${message}"`);
}

// ---------------------------------------------------------------- strict link

function strictLinkProbes(scratch) {
  const probes = [
    ['probe_undefined', 'extern int ms_probe_missing(int);\n' +
      '__attribute__((export_name("probe"))) int probe(int x) { return ms_probe_missing(x); }\n',
      'undefined symbol: ms_probe_missing'],
    ['probe_multi3', 'typedef unsigned __int128 u128;\n' +
      '__attribute__((export_name("probe"))) unsigned long long probe(unsigned long long a,\n' +
      '    unsigned long long b) { return (unsigned long long)(((u128)a * b) >> 64); }\n',
      'undefined symbol: __multi3'],
  ];
  for (const [name, source, expected] of probes) {
    const file = relative(ROOT, join(scratch, `${name}.c`));
    writeFileSync(join(ROOT, file), source);
    const result = tryLinkWasm(file, join(scratch, `${name}.wasm`));
    const output = `${result.stdout}${result.stderr}`;
    check(result.status !== 0, `${name} must fail to link, but linked`);
    check(output.includes(expected), `${name} must report "${expected}", got:\n${output}`);
    console.log(`strict link: ${name} rejected (${expected})`);
  }
}

// The pre-compile policy scan rejects system headers and 128-bit integers,
// following quoted includes, but ignores comments and strings.
function sourcePolicyProbes(scratch) {
  const dir = relative(ROOT, scratch);
  const write = (name, text) => {
    writeFileSync(join(ROOT, dir, name), text);
    return join(dir, name);
  };
  const clean = write('policy_ok.c', '/* no __int128, no <stdio.h> */\n#include <base/types.h>\n' +
    'static const char *s = "__int128 #include <stdio.h>";\n');
  checkSourcePolicy([clean]);
  write('policy_bad.h', 'typedef unsigned __int128 wide;\n');
  const cases = [
    [write('policy_stdio.c', '#include <stdio.h>\n'), 'system header <stdio.h>'],
    [write('policy_int128.c', '#include "policy_bad.h"\n'), 'uses a 128-bit integer type'],
    [write('policy_memset.c', 'void *memset(void *s, int c, unsigned long n);\n' +
      'void clear(char *p) { memset(p, 0, 8); }\n'), 'uses the C library memory function memset()'],
  ];
  for (const [file, expected] of cases) {
    rejects(() => checkSourcePolicy([file]), expected, file);
    console.log(`source policy: ${basename(file)} rejected (${expected})`);
  }
  checkSourcePolicy(['c/compiler_mem.c']);
  check(true, 'the Windows compiler memory adapters may define memset/memcpy');
}

// The Windows audits (scripts/build.mjs) read dumpbin output, so they are
// probed here on every platform: the forwarding adapters pass, while
// recursion (an optimized base_memset calling memset), unreadable output and
// a memory helper imported from a DLL are rejected.
function windowsAuditProbes() {
  const coff = (symbols) => ['', 'Dump of file build\\native\\obj\\x.obj', '', 'File Type: COFF OBJECT',
    '', 'COFF SYMBOL TABLE', '000 01047A8F ABS    notype       Static       | @comp.id',
    '003 00000000 SECT1  notype       Static       | .drectve',
    '    Section length   2F, #relocs    0, #linenums    0, checksum        0',
    ...symbols.map(([section, name], i) => `${(9 + i).toString(16).toUpperCase().padStart(3, '0')} ` +
      `00000000 ${section.padEnd(6)} notype ()    External     | ${name}`),
    '', 'String Table Size = 0x2F bytes', ''].join('\r\n');
  const adapter = parseCoffSymbols(coff([['SECT3', 'memset'], ['SECT3', 'memcpy'],
    ['UNDEF', 'base_memset'], ['UNDEF', 'base_memcpy'], ['UNDEF', '__chkstk']]));
  const mem = parseCoffSymbols(coff([['SECT3', 'base_memcpy'], ['SECT3', 'base_memset'],
    ['UNDEF', '__chkstk']]));
  same([...adapter.referenced].join(' '), 'base_memset base_memcpy __chkstk', 'dumpbin /symbols references');
  checkMemoryAdapters(adapter, mem);
  check(true, 'forwarding memory adapters accepted');
  const looping = parseCoffSymbols(coff([['SECT3', 'base_memcpy'], ['SECT3', 'base_memset'],
    ['UNDEF', 'memset']]));
  rejects(() => checkMemoryAdapters(adapter, looping), 'memory helper recursion',
    'an optimized base_memset that calls memset');
  rejects(() => checkMemoryAdapters(parseCoffSymbols(''), mem), 'must define memset/memcpy',
    'unreadable dumpbin /symbols output');
  const imports = (names) => parseDumpbinImports(['', 'Dump of file ms_tests.exe', '',
    'File Type: EXECUTABLE IMAGE', '', '  Section contains the following imports:', '',
    '    KERNEL32.dll', '             140010000 Import Address Table',
    '                     0 time date stamp', '',
    ...names.map((name, i) => `                         ${(0x2D5 + i).toString(16).toUpperCase()} ${name}`),
    '', '  Summary', '', '        1000 .data', ''].join('\r\n'));
  same(imports(['GetStdHandle', 'ExitProcess']).join(' '), 'GetStdHandle ExitProcess',
    'dumpbin /imports names');
  checkWindowsImports(imports(['GetStdHandle', 'ExitProcess']), 'ms_tests.exe');
  rejects(() => checkWindowsImports(imports(['ExitProcess', 'memset']), 'ms_tests.exe'),
    'imports memory helper(s) memset', 'memset imported from a DLL');
  rejects(() => checkWindowsImports([], 'ms_tests.exe'), 'cannot read the imports',
    'unreadable dumpbin /imports output');
  console.log('windows audits: forwarding adapters accepted; recursion, DLL memory helpers and ' +
    'unreadable dumpbin output rejected');
}

// The production export list comes from export_name markers in
// c/wasm_api.{h,c}; undocumented, misnamed or missing exports are refused.
function exportDeclarationProbes() {
  const header = [
    '#pragma once',
    '#include <base/types.h>',
    '#if defined(__wasm__)',
    '#define MS_EXPORT(name) __attribute__((export_name(#name))) name',
    '#else',
    '#define MS_EXPORT(name) name',
    '#endif',
    '/* MS_EXPORT(ms_commented) and export_name("ms_comment") are documentation only. */',
    'uint32_t MS_EXPORT(ms_abi_version)(void);',
    'int32_t MS_EXPORT(ms_init)(void);',
    'int32_t MS_EXPORT(ms_game_new)(uint32_t width, uint32_t height);',
    'uint32_t ms_view_size(void);',
  ].join('\n');
  const source = [
    '#include "wasm_api.h"',
    'uint32_t MS_EXPORT(ms_abi_version)(void) { return 1; }',
    'int32_t MS_EXPORT(ms_init)(void) { return 0; }',
    'int32_t MS_EXPORT(ms_game_new)(uint32_t w, uint32_t h) { return (int32_t)(w + h); }',
    '__attribute__((export_name("ms_view_size"))) uint32_t ms_view_size(void) { return 0; }',
  ].join('\n');
  const good = parseExportDeclarations(header, source);
  same(good.problems.join('; '), '', 'well-formed export declarations');
  same(good.names.join(' '), 'ms_abi_version ms_game_new ms_init ms_view_size', 'declared exports');
  const bad = [
    [`${source}\nvoid MS_EXPORT(ms_secret)(void) {}`, 'export ms_secret is not declared in c/wasm_api.h'],
    [`${source}\n__attribute__((export_name("free"))) void f(void) {}`, 'export "free" does not match'],
    [source.replace('MS_EXPORT(ms_init)', 'ms_init'), 'required export ms_init (engine.h) is not declared'],
  ];
  for (const [text, expected] of bad) {
    const parsed = parseExportDeclarations(header.replace('MS_EXPORT(ms_init)', 'ms_init'), text);
    check(parsed.problems.some((problem) => problem.includes(expected)),
      `export declarations must report "${expected}", got: ${parsed.problems.join('; ')}`);
  }
  console.log('export declarations: undocumented, misnamed and missing exports rejected');
}

// The dist/ reference check accepts relative resources that exist and
// rejects external, absolute, missing and bare-specifier references, while
// ignoring comments, string contents, data: URLs, fragments and links out.
function siteReferenceProbes(scratch) {
  const site = join(scratch, 'check-site');
  mkdirSync(site, { recursive: true });
  const files = {
    'index.html': [
      '<!doctype html><html><head>',
      '<link rel="icon" href="data:image/svg+xml,%3Csvg%3E%3C/svg%3E">',
      '<link rel="stylesheet" href="styles.css">',
      '<script type="module" src="./app.js"></script>',
      '<script src="https://cdn.example.com/lib.js"></script>',
      '<!-- <script src="/commented-out.js"></script> -->',
      '</head><body>',
      '<img src="/logo.png" alt="">',
      '<a href="https://example.com/">project</a> <a href="#top">top</a>',
      '<svg><use href="#i-flag"></use></svg>',
      "<script type=\"module\">import { a } from './lib.js'; import b from './missing-inline.js';</script>",
      '</body></html>',
    ].join('\n'),
    'styles.css': '/* url(in-comment.png) */\nbody { background: url("bg.png"); }\n' +
      '.x { background-image: url(data:image/png;base64,AAAA); }\n',
    'app.js': [
      "// import gone from './commented.js';",
      "import { a } from './lib.js';",
      "import thing from 'bare-package';",
      "export * from './lib.js';",
      "const lazy = import('./lazy.js');",
      "const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });",
      "const data = fetch(new URL('./data.json', import.meta.url));",
      "fetch('/api/games');",
      "const text = \"fetch('/not-code') import x from 'nope'\";",
    ].join('\n'),
    'lib.js': 'export const a = 1;\n',
    'lazy.js': 'export default 2;\n',
    'data.json': '{}\n',
    'worker.js': "import { makeWasi } from './vendor/corec/wasi.js';\n",
  };
  for (const [name, text] of Object.entries(files)) writeFileSync(join(site, name), text);
  const problems = checkSiteReferences(site);
  const expected = [
    "index.html: 'https://cdn.example.com/lib.js' is external",
    "index.html: '/logo.png' is an absolute path",
    "index.html: './missing-inline.js' does not exist",
    "styles.css: 'bg.png' does not exist",
    "app.js: 'bare-package' is a bare module specifier",
    "app.js: '/api/games' is an absolute path",
    "worker.js: './vendor/corec/wasi.js' does not exist",
  ];
  for (const text of expected) {
    check(problems.some((problem) => problem.includes(text)), `site check must report "${text}"`);
  }
  same(problems.length, expected.length, `site check problems (${problems.join('; ')})`);
  console.log('site references: external, absolute, missing and bare references rejected');
}

// dist/ verification (build.mjs verifyDist) accepts exactly the registered
// release files. It runs here on a scratch copy of that set (the current
// static/ files, corec's vendor files and a stand-in reactor), never on the
// real dist/: anything else, such as a stray Python or test file, or a
// missing asset, must fail it.
function distFileProbes(scratch) {
  const site = join(scratch, 'dist-files');
  const wasm = join(scratch, 'stand-in.wasm');
  writeFileSync(wasm, Uint8Array.of(0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00));
  const add = (name, source) => {
    mkdirSync(dirname(join(site, name)), { recursive: true });
    copyFileSync(source, join(site, name));
  };
  STATIC_FILES.forEach((name) => add(name, join(ROOT, 'static', name)));
  add(REACTOR_FILE, wasm);
  Object.entries(VENDOR_FILES).forEach(([name, source]) => add(name, join(ROOT, source)));
  verifyDist(site, wasm);
  check(true, 'the registered release files pass dist/ verification');
  const strays = [['server.py', 'print("not part of the site")\n'],
    ['tests/app.test.mjs', 'export {};\n'], ['.env', 'SECRET=1\n']];
  for (const [name, text] of strays) {
    mkdirSync(dirname(join(site, name)), { recursive: true });
    writeFileSync(join(site, name), text);
    rejects(() => verifyDist(site, wasm), `unexpected dist/${name}`, `a stray ${name} in dist/`);
    rmSync(join(site, name.split('/')[0]), { recursive: true });
  }
  verifyDist(site, wasm);
  rmSync(join(site, 'app.js'));
  rejects(() => verifyDist(site, wasm), 'missing dist/app.js', 'a missing app.js');
  console.log('dist files: stray .py, test and dotfiles and a missing asset rejected');
}

// ---------------------------------------------------------------- reactor

function makeIo(program) {
  return {
    argv: [program],
    environ: [],
    stdin: { read: () => new Uint8Array() },
    stdout: { write: (bytes) => process.stdout.write(bytes) },
    stderr: { write: (bytes) => process.stderr.write(bytes) },
    fs: {
      open: () => -1, close: () => 8, read: () => null, write: () => -1,
      seek: () => null, tell: () => null,
    },
  };
}

async function reactorSmoke(module) {
  let hostNow = 0;
  const wasi = makeWasi(makeIo('reactor_smoke'));
  const instance = await WebAssembly.instantiate(module, {
    ...wasi.imports,
    ms_host: { now_ms: () => hostNow },
  });
  const x = instance.exports;
  const memory = x.memory;
  wasi.setMemory(memory);
  const u64 = (value) => BigInt.asUintN(64, value);

  // Initialization happens exactly once, under host control.
  same(x.smoke_abi_version(), abiVersion(), 'ABI version before init');
  same(x.smoke_alloc(64), 0, 'allocation before init is refused');
  same(x.smoke_capacity(0x10000), 0, 'nothing is owned before init');
  same(x.smoke_init(), MS_OK, 'first init');
  same(x.smoke_init(), MS_ERR_INTERNAL, 'second init is refused');

  // Aligned, budgeted buffers.
  const a = x.smoke_alloc(100);
  check(a !== 0 && a % 16 === 0, `allocation is 16-byte aligned (${a})`);
  same(x.smoke_live_bytes(), 4096, 'live bytes after one small buffer');
  same(x.smoke_alloc(64 << 20), 0, 'request above the 64 MiB budget is refused');

  // Ownership is proven against the context's own allocations before any
  // header is read: low, foreign, interior, past-the-end, beyond-memory and
  // high pointers are refused as misuse, never a trap, and change nothing.
  const capacity = x.smoke_capacity(a);
  check(capacity >= 100 && capacity < 4096, `capacity of a 100-byte buffer (${capacity})`);
  const heapBase = x.__heap_base.value;
  check(heapBase !== a, '__heap_base is not an allocation');
  const refused = [1, 16, 4096, heapBase, a + 16, a + capacity,
    memory.buffer.byteLength + 16, 0xFFFFFFF0, 0xFFFFFFFF];
  for (const ptr of refused) {
    same(x.smoke_capacity(ptr), 0, `no capacity at ${ptr}`);
    same(x.smoke_free(ptr), MS_ERR_INTERNAL, `free(${ptr}) is refused without a trap`);
  }
  same(x.smoke_live_bytes(), 4096, 'refused frees changed nothing');
  same(x.smoke_capacity(a), capacity, 'the buffer is still owned');
  same(u64(x.smoke_hash(a, capacity + 1, 0n)), 0n, 'a range past the buffer is refused');
  same(x.smoke_neighbors(80, 80, 81, a + 4), 0, 'an interior out-pointer is refused');
  same(x.smoke_neighbors(80, 80, 81, 0xFFFFFFF0), 0, 'a wild out-pointer is refused');

  new Uint8Array(memory.buffer, a, 100).set(Array.from({ length: 100 }, (_, i) => i));
  same(u64(x.smoke_hash(a, 100, 0n)), 0x2f18c13ee3697bcen, 'rt_hash64 known answer');
  same(u64(x.smoke_rng(0n, 0)), 0x99ec5f36cb75f2b4n, 'xoshiro256** seed 0 first output');
  same(u64(x.smoke_rng(0x0123456789abcdefn, 3)), 0x1ba4ddc6fe2b5726n, 'xoshiro256** 4th output');
  const n = x.smoke_neighbors(80, 80, 81, a);
  same(n, 8, 'neighbor count');
  same(JSON.stringify(Array.from(new Uint32Array(memory.buffer, a, 8))),
    '[0,1,2,80,82,160,161,162]', 'ascending neighbors of (1, 1) on 80x80');
  same(x.smoke_free(a), MS_OK, 'free');
  same(x.smoke_free(a), MS_ERR_INTERNAL, 'a double free is refused');
  same(x.smoke_capacity(a), 0, 'a freed buffer has no capacity');
  same(x.smoke_free(12345), MS_ERR_INTERNAL, 'freeing a foreign pointer is refused');
  same(x.smoke_live_bytes(), 0, 'live bytes after free');

  // Memory growth detaches existing views; callers must recreate them.
  const before = memory.buffer;
  const big = x.smoke_alloc((16 << 20) - 4096);
  check(big !== 0 && big % 16 === 0, 'large allocation succeeds');
  check(before.byteLength === 0 && memory.buffer !== before,
    'memory.grow detached the old ArrayBuffer');
  const view = new Uint8Array(memory.buffer, big, (16 << 20) - 4096);
  view[0] = 7;
  view[view.length - 1] = 9;
  same(view[0] + view[view.length - 1], 16, 'refreshed view reads and writes');
  same(x.smoke_free(big), MS_OK, 'free large buffer');

  // The injected host clock: finite and monotonic.
  hostNow = 1000;
  same(x.smoke_clock_ms(), 1000, 'clock reads ms_host.now_ms');
  hostNow = 900;
  same(x.smoke_clock_ms(), 1000, 'clock never goes backwards');
  hostNow = NaN;
  same(x.smoke_clock_ms(), 1000, 'NaN readings are ignored');
  hostNow = 1500.5;
  same(x.smoke_clock_ms(), 1500.5, 'clock advances');

  abiMarshalling(x, memory);
  same(x.smoke_live_bytes(), 0, 'every smoke buffer was released');

  // proc_exit unwinds as corec's ProcExit; the instance is then discarded.
  let exited = null;
  try {
    x.smoke_exit(3);
  } catch (error) {
    if (!(error instanceof ProcExit)) throw error;
    exited = error.status;
  }
  same(exited, 3, 'proc_exit propagates as ProcExit');
}

// Builds ABI buffers from JS exactly as the adapter will (little-endian
// DataView at documented offsets) and lets C validate them.
function abiMarshalling(x, memory) {
  const buffer = (size) => {
    const ptr = x.smoke_alloc(size);
    check(ptr !== 0 && ptr % 8 === 0, `buffer of ${size} bytes`);
    new Uint8Array(memory.buffer, ptr, size).fill(0);
    return ptr;
  };
  const dv = () => new DataView(memory.buffer);

  // Observation of Python's (4, 1, 1, {0: 1, 3: 0}).
  const obs = buffer(40);
  [0x4D530002, 1, 4, 1, 1, 2, 0, 0].forEach((v, i) => dv().setUint32(obs + 4 * i, v, true));
  new Uint8Array(memory.buffer, obs + 32, 4).set([1, 0xFF, 0xFF, 0]);
  same(x.smoke_obs_validate(obs, 40), MS_OK, 'JS-built observation');
  dv().setUint8(obs + 33, 9);
  same(x.smoke_obs_validate(obs, 40), MS_ERR_INVALID_OBSERVATION, 'clue 9 is rejected');
  dv().setUint8(obs + 33, 0xFF);
  same(x.smoke_obs_validate(obs + 4, 40), MS_ERR_INVALID_BUFFER, 'interior observation pointer');
  same(x.smoke_obs_validate(1, 40), MS_ERR_INVALID_BUFFER, 'wild observation pointer');
  same(x.smoke_obs_validate(obs, x.smoke_capacity(obs) + 1), MS_ERR_INVALID_BUFFER,
    'observation length past its buffer');
  same(x.smoke_limits_validate(0xFFFFFFF0), MS_ERR_INVALID_BUFFER, 'wild limits pointer');
  same(x.smoke_view_validate(memory.buffer.byteLength + 16, 80), MS_ERR_INVALID_BUFFER,
    'view pointer beyond memory');

  // Its exact result, filled from JS after C wrote the header and hash.
  const res = buffer(160);
  same(x.smoke_result_init(res, 160, obs, 40), MS_OK, 'result header from C');
  same(x.smoke_result_init(res + 8, 152, obs, 40), MS_ERR_INVALID_BUFFER, 'interior result pointer');
  same(x.smoke_result_validate(obs, 40, res, 160), MS_ERR_INVALID_RESULT, 'status 0 is invalid');
  const r = dv();
  r.setUint32(res + 8, 1, true);    // status: exact
  r.setUint32(res + 80, 2, true);   // propagated_cells
  r.setUint32(res + 84, 1, true);   // pair_reasoning_complete
  r.setUint32(res + 88, 1, true);   // proven_safe
  r.setUint32(res + 92, 1, true);   // proven_mines
  r.setFloat64(res + 104, 0.5, true);
  r.setFloat64(res + 120 + 8 * 1, 1.0, true);
  r.setFloat64(res + 120 + 8 * 2, 0.0, true);
  r.setUint8(res + 152 + 1, 0x01 | 0x04); // value + proven mine
  r.setUint8(res + 152 + 2, 0x01 | 0x02); // value + proven safe
  same(x.smoke_result_validate(obs, 40, res, 160), MS_OK, 'JS-filled exact result');

  // What the adapter will rebuild for app.js (pure marshalling).
  const cells = 4;
  const flags = new Uint8Array(memory.buffer, res + 120 + 8 * cells, cells);
  const values = new Float64Array(memory.buffer, res + 120, cells);
  const probabilities = Array.from(values, (v, i) => (flags[i] & 1 ? v : null));
  const provenSafe = [...flags.keys()].filter((i) => flags[i] & 2);
  const provenMines = [...flags.keys()].filter((i) => flags[i] & 4);
  same(JSON.stringify(probabilities), '[null,1,0,null]', 'probabilities');
  same(JSON.stringify([provenSafe, provenMines]), '[[2],[1]]', 'proven cells');
  r.setUint8(res + 152 + 2, 0x02);
  same(x.smoke_result_validate(obs, 40, res, 160), MS_ERR_INVALID_RESULT,
    'a proof without a value is rejected');

  // Inference limits with the default policy.
  const limits = buffer(56);
  const l = dv();
  [0x4D530003, 1, 100000, 2000, 1500000, 0].forEach((v, i) => l.setUint32(limits + 4 * i, v, true));
  l.setFloat64(limits + 24, 1500, true);
  l.setFloat64(limits + 32, 50, true);
  l.setBigUint64(limits + 40, 256n << 20n, true);
  same(x.smoke_limits_validate(limits), MS_OK, 'JS-built default limits');
  l.setFloat64(limits + 24, NaN, true);
  same(x.smoke_limits_validate(limits), MS_ERR_INVALID_LIMITS, 'NaN time budget');

  // A ready 5x5 view: every cell hidden (adjacency nibble 0xF).
  const view = buffer(80);
  [0x4D530001, 1, 1, 0, 1, 5, 5, 3, 0, 0].forEach((v, i) => dv().setUint32(view + 4 * i, v, true));
  new Uint8Array(memory.buffer, view + 48, 25).fill(0x0F);
  same(x.smoke_view_validate(view, 80), MS_OK, 'JS-built ready view');

  [obs, res, limits, view].forEach((ptr) => same(x.smoke_free(ptr), MS_OK, 'free ABI buffer'));
}

// The production reactor instantiates with exactly corec's WASI host and
// the ms_host boundary, and follows engine.h's initialization rules.
async function productionSmoke(path) {
  const wasi = makeWasi(makeIo('minesweeper'));
  const { instance } = await WebAssembly.instantiate(readFileSync(path), {
    ...wasi.imports,
    ms_host: { now_ms: () => performance.now() },
  });
  wasi.setMemory(instance.exports.memory);
  const x = instance.exports;
  const version = abiVersion();
  same(x.ms_abi_version(), version, 'production ms_abi_version() before ms_init()');
  same(x.ms_init(), MS_OK, 'production ms_init()');
  same(x.ms_init(), MS_ERR_INTERNAL, 'production second ms_init() is refused');
  same(x.ms_abi_version(), version, 'production ms_abi_version() after ms_init()');
  plannerSmoke(x);
  console.log(`production reactor: ${path} instantiated; ABI version ${version}, one-time ms_init, ` +
    `advisor exports ${PLANNER_EXPORTS.join(', ')}`);
}

// The move advisor's exports (wasm_api.h): fixed-size default limits, every
// buffer refused before a byte is touched (aliasing, length, wild
// pointers), a canonical plan for a tiny observation, a workspace released
// after every call, and a game-instance check that never accepts a plan
// without a current game.
function plannerSmoke(x) {
  const memory = x.memory;
  const dv = () => new DataView(memory.buffer);
  const buffer = (size) => {
    const ptr = x.ms_alloc(size) >>> 0;
    check(ptr !== 0 && ptr % 16 === 0, `production buffer of ${size} bytes`);
    return ptr;
  };
  const limits = buffer(64);
  same(x.ms_init_plan_limits(limits, 56), MS_ERR_INVALID_BUFFER, 'plan limits need exactly 64 bytes');
  same(x.ms_init_plan_limits(limits + 8, 56), MS_ERR_INVALID_BUFFER, 'interior plan limits');
  same(x.ms_init_plan_limits(limits, 64), MS_OK, 'default plan limits');
  same(dv().getUint32(limits, true), 0x4D530005, 'plan limits magic');
  same(dv().getUint32(limits + 4, true), 1, 'planner version');
  const budget = dv().getFloat64(limits + 32, true);
  check(Number.isFinite(budget) && budget > 0, `finite default plan time budget (${budget})`);

  // Python's (4, 1, 1, {0: 1, 3: 0}), as in abiMarshalling.
  const obs = buffer(40);
  [0x4D530002, 1, 4, 1, 1, 2, 0, 0].forEach((v, i) => dv().setUint32(obs + 4 * i, v, true));
  new Uint8Array(memory.buffer, obs + 32, 4).set([1, 0xFF, 0xFF, 0]);
  const plan = buffer(112);
  new Uint8Array(memory.buffer, plan, 112).fill(0xA5);
  const bytes = () => Array.from(new Uint8Array(memory.buffer, obs, 40)).join() +
    Array.from(new Uint8Array(memory.buffer, limits, 64)).join() +
    Array.from(new Uint8Array(memory.buffer, plan, 112)).join();
  const before = bytes();
  const refused = [
    [obs, 40, limits, 64, obs, 112, 'a plan over its own observation'],
    [obs, 40, limits, 64, limits, 112, 'a plan over its limits'],
    [obs, 40, obs, 64, plan, 112, 'limits over the observation'],
    [obs, 40, limits, 56, plan, 112, 'solver-sized limits'],
    [obs, 40, limits, 64, plan, 120, 'a result-sized plan'],
    [obs, 40, limits, 64, plan + 8, 112, 'an interior plan crossing its buffer'],
    [obs, 40, limits, 64, 0xFFFFFFF0, 112, 'a wild plan pointer'],
    [1, 40, limits, 64, plan, 112, 'a wild observation pointer'],
  ];
  for (const [o, ol, l, ll, r, rl, what] of refused) {
    same(x.ms_plan_observation(o, ol, l, ll, r, rl), MS_ERR_INVALID_BUFFER, `${what} is refused`);
  }
  same(bytes(), before, 'refused plans change no buffer');
  same(x.ms_live_bytes(2), 0, 'refused plans hold no workspace');
  same(x.ms_plan_observation(obs, 40, limits, 64, plan, 112), MS_OK, 'a plan for a tiny position');
  same(dv().getUint32(plan, true), 0x4D530006, 'plan magic');
  same(dv().getUint32(plan + 4, true), 1, 'plan version');
  check(dv().getUint32(plan + 8, true) <= 3, 'plan status none/exact/estimated/unavailable');
  same(x.ms_live_bytes(2), 0, 'the planner releases its workspace');
  same(x.ms_check_plan(1, 0, plan, 104), MS_ERR_INVALID_BUFFER, 'ms_check_plan needs a 112-byte plan');
  same(x.ms_check_plan(1, 0, plan, 112), MS_ERR_STALE_REVISION, 'without a game no plan is current');
  for (const ptr of [obs, limits, plan]) same(x.ms_free(ptr), MS_OK, 'free advisor smoke buffer');
  same(x.ms_live_bytes(0), 0, 'every advisor smoke buffer was freed');
}

// ---------------------------------------------------------------- main

async function main(argv) {
  const selection = parseSuites(argv);
  if (selection.rest.length) throw new BuildError(`unexpected argument(s): ${selection.rest.join(' ')}`);
  audited(buildWasm(selection.suites), { functions: ['_start', ...COREC_WASM_EXPORTS] });
  mkdirSync(join(ROOT, 'build'), { recursive: true });
  const scratch = mkdtempSync(join(ROOT, 'build', 'check-wasm-'));
  try {
    const smoke = audited(buildSmoke(scratch), {
      functions: SMOKE_FUNCTIONS, hostImports: HOST_IMPORTS, requireHost: ['ms_host.now_ms'],
    });
    strictLinkProbes(scratch);
    sourcePolicyProbes(scratch);
    windowsAuditProbes();
    exportDeclarationProbes();
    siteReferenceProbes(scratch);
    distFileProbes(scratch);
    await reactorSmoke(smoke);
  } finally {
    rmSync(scratch, { recursive: true, force: true });
  }
  if (selection.all) {
    const reactor = buildReactor();
    check(true, 'production reactor audit');
    await productionSmoke(reactor);
  } else {
    console.log('production reactor: NOT checked in this --suite subset run ' +
      '(run check-wasm without --suite for the full check)');
  }
  console.log(`check-wasm: ${checks} checks passed`);
}

try {
  await main(process.argv.slice(2));
} catch (error) {
  console.error(`check-wasm.mjs: ${error.message}`);
  process.exitCode = error instanceof BuildError ? 2 : 1;
}
