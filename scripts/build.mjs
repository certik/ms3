#!/usr/bin/env node
// Build driver for the freestanding C sources and the static site, following
// corec's conventions (-nostdlib -nostdinc -fno-builtin, corec's
// platform_<os>.c, no libc, no CRT):
//
//   node scripts/build.mjs <action> [--suite NAME[,NAME...]]...
//
//   native        build/native/tests-<id>/ms_tests[.exe]: host OS C test runner
//   wasm          build/wasm/tests-<id>/ms_tests.wasm: corec _start/app_main C test runner
//   test-native   native, then run it
//   test-wasm     wasm, then run it under Wasmtime
//   test-node     wasm, then run it under Node with corec's WASI host
//   smoke         build/wasm/reactor_smoke.wasm: test-only reactor (check-wasm
//                 builds its own copy in a private scratch directory)
//   reactor       build/wasm/minesweeper.wasm: production reactor, audited
//   dist          reactor + static/ + corec's wasi.js and MIT license in
//                 dist/, then checks that dist/ holds exactly the registered
//                 release files (STATIC_FILES) and every site reference
//
// Suites: runtime, bigint, game, probability, posterior, planner, autosolve,
// api (default: all).
// A suite is compiled in together with exactly the sources listed in SUITES,
// so a subset builds while other modules are unfinished. The default (all
// suites), the production reactor and dist need every source and fail with
// the list of missing files: nothing is skipped and no placeholder module is
// ever produced.
//
// Build identity: each suite set builds in its own directory tests-<id>,
// where <id> is "all" or the selected suites in SUITE_NAMES order joined by
// "-" (--suite game,runtime -> tests-runtime-game), with its own objects.
// Concurrent builds of different suite sets therefore never share a file,
// and test-native/test-wasm/test-node run the binary they just built. The
// production reactor and dist/ have their own fixed paths.
//
// Toolchains: WebAssembly and native macOS/Linux use pixi's Clang/lld from
// the active pixi environment (run through `pixi run`), never a system
// compiler. Windows uses MSVC cl/link/dumpbin from a vcvars64 environment,
// as corec does.
//
// Library policy, enforced by every build:
// - Project sources include only corec headers (<base/...>, <platform/...>),
//   never use 128-bit integers and never call memset/memcpy/... by name
//   (checkSourcePolicy; c/compiler_mem.c alone defines them, for Windows).
// - WebAssembly links without --allow-undefined, so an unresolved libc or
//   compiler-rt symbol is a link error; check-wasm audits imports/exports.
// - macOS binaries import from libSystem only the OS calls that corec's
//   platform_macos.c declares, plus the compiler's stack-protector hooks.
// - Linux binaries link no library. corec's own base objects may use the
//   weak memcpy/memset of corec's platform_linux.c (corec code, not libc);
//   project objects may not reference any memory helper (linker trace).
// - Windows binaries link /nodefaultlib against kernel32/shell32 only, so an
//   unresolved helper fails the link; dumpbin checks the DLL dependencies and
//   that no memory helper is imported. cl/clang-cl emit memset/memcpy for
//   local aggregates even without /O, so c/compiler_mem.c (Windows only)
//   forwards exactly those to corec's base_mem*; dumpbin /symbols proves the
//   adapter and corec's mem object reference no memory helper (no recursion).
// Compiler-generated memory helper calls therefore never resolve to libc.
// Project code never calls them by name (source policy), and a new helper is
// added only as a narrowly scoped adapter over corec's base_mem* routines in
// c/compiler_mem.c, never by linking a C library.

import { spawnSync } from 'node:child_process';
import {
  copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync,
} from 'node:fs';
import {
  basename, delimiter, dirname, extname, isAbsolute, join, relative, resolve, sep,
} from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const COREC = 'third_party/corec';

const COREC_BASE = ['io', 'buddy', 'arena', 'scratch', 'format', 'math', 'string', 'strbuf',
  'mem', 'numconv', 'assert', 'exit'].map((name) => `${COREC}/base/${name}.c`);

// Suite -> its test file and the engine sources it needs. Keep each list
// minimal; extend it when a module gains files.
export const SUITES = {
  runtime: { test: 'tests/c/test_runtime.c', sources: ['c/runtime.c'] },
  bigint: { test: 'tests/c/test_bigint.c', sources: ['c/runtime.c', 'c/bigint.c'] },
  game: { test: 'tests/c/test_game.c', sources: ['c/runtime.c', 'c/game.c'] },
  probability: {
    test: 'tests/c/test_probability.c',
    sources: ['c/runtime.c', 'c/bigint.c', 'c/probability.c'],
  },
  // Complete-layout posterior generation (ms_posterior_generate, c/posterior.h),
  // which reuses the solver's counting and sampling in c/probability.c.
  posterior: {
    test: 'tests/c/test_posterior.c',
    sources: ['c/runtime.c', 'c/bigint.c', 'c/probability.c'],
  },
  // Move advisor (ms_plan, c/planner.h) over posterior layouts.
  planner: {
    test: 'tests/c/test_planner.c',
    sources: ['c/runtime.c', 'c/bigint.c', 'c/probability.c', 'c/planner.c'],
  },
  // Integration: real solver results feeding atomic autosolve batches.
  autosolve: {
    test: 'tests/c/test_autosolve.c',
    sources: ['c/runtime.c', 'c/bigint.c', 'c/game.c', 'c/probability.c', 'c/engine.c'],
  },
  // Engine service unit tests, independent of the solver (no bigint or
  // probability: engine.c must not call ms_solve).
  api: { test: 'tests/c/test_api.c', sources: ['c/runtime.c', 'c/game.c', 'c/engine.c'] },
};
export const SUITE_NAMES = Object.keys(SUITES);

const RUNNER = 'tests/c/main.c';
const SMOKE_SOURCE = 'tests/c/reactor_smoke.c';
// The production reactor compiles every c/*.c file; all of these must exist.
export const ENGINE_SOURCES = ['c/runtime.c', 'c/bigint.c', 'c/game.c', 'c/probability.c',
  'c/planner.c', 'c/engine.c', 'c/wasm_api.c'];
// memset/memcpy forwarding to corec's base_mem*, linked only into the Windows
// native build (see the file): the one project file that may define them, and
// never part of the reactor.
const COMPILER_MEM_SOURCE = 'c/compiler_mem.c';
const COMPILER_MEM_HELPERS = ['memset', 'memcpy'];
const WASM_API_HEADER = 'c/wasm_api.h';
const WASM_API_SOURCE = 'c/wasm_api.c';
export const REACTOR_FILE = 'minesweeper.wasm';
const STATIC_DIR = 'static';
const DIST_DIR = 'dist';
// The site's own files, copied from static/. dist/ verification rejects
// every file that is not one of these, the reactor or VENDOR_FILES, so a new
// asset must be registered here before it can ship.
export const STATIC_FILES = ['index.html', 'styles.css', 'app.js', 'engine-client.js',
  'wasm-host.js', 'probability-worker.js'];
// Copied verbatim into dist/: corec's JS WASI host and its MIT license.
export const VENDOR_FILES = {
  'vendor/corec/wasi.js': `${COREC}/platform/js/wasi.js`,
  'vendor/corec/LICENSE': `${COREC}/LICENSE`,
};

const PROJECT_INCLUDES = ['c', 'tests/c'];
const COREC_INCLUDES = [`${COREC}/platform`, COREC];

// corec's freestanding flags, plus -ffp-contract=off so native FMA
// contraction cannot change doubles relative to WebAssembly.
const FREESTANDING = ['-nostdlib', '-nostdinc', '-fno-builtin', '-ffp-contract=off'];
const WARNINGS = ['-Wall', '-Wextra', '-Werror', '-Wvla'];
const WASM_TARGET = ['--target=wasm32-wasi'];
const WASM_TEST_OPT = ['-Os'];     // corec's flags for its test module
const WASM_REACTOR_OPT = ['-O2'];  // production: solver speed over size
const WASM_LINK_COMMON = [
  '-Wl,--no-entry',
  '-Wl,--export=__heap_base',
  '-Wl,--stack-first',
  '-Wl,-z,stack-size=1048576',
  '-Wl,--fatal-warnings',
];
const WASM_TEST_LINK = [...WASM_LINK_COMMON, '-Wl,--export=_start'];
const WASM_REACTOR_LINK = [...WASM_LINK_COMMON];

// The application host boundary (engine.h, "Reactor ABI rules").
export const HOST_IMPORTS = new Set(['ms_host.now_ms']);
// The move advisor's additive exports (wasm_api.h): the production reactor
// must declare each of them, like engine.h's ms_abi_version and ms_init.
export const PLANNER_EXPORTS = ['ms_init_plan_limits', 'ms_plan_observation', 'ms_check_plan'];
// Exported by corec's platform_wasm.c itself; JS never uses them.
export const COREC_WASM_EXPORTS = ['wasm_buddy_alloc', 'wasm_buddy_free'];

// Memory routines compilers may emit calls to for aggregate copies and
// initializers. They must never resolve to a C library.
export const MEMORY_HELPERS = ['memcpy', 'memmove', 'memset', 'memcmp', 'bcmp', 'bzero'];
const MEMORY_HELPER_ADVICE =
  'Compiler-generated memory helper calls must not resolve to libc, and project code may not ' +
  "rely on corec's weak Linux fallbacks. Avoid large by-value aggregate copies or initializers. " +
  "If a toolchain must emit a helper, forward exactly that helper to corec's base_mem* routines " +
  `in ${COMPILER_MEM_SOURCE} (today linked on Windows only, for ${COMPILER_MEM_HELPERS.join('/')}) ` +
  'and extend scripts/build.mjs and its recursion audit, instead of linking a C library.';
// macOS: compiler-emitted stack-protector hooks, besides corec's OS calls.
const MACOS_STACK_PROTECTOR = ['___stack_chk_fail', '___stack_chk_guard'];
// Windows: corec links /nodefaultlib against these import libraries only.
const WINDOWS_DLLS = ['kernel32.dll', 'shell32.dll'];

export class BuildError extends Error {}

function fail(message) {
  throw new BuildError(message);
}

function rel(path) {
  return relative(ROOT, path) || '.';
}

function isFile(path) {
  try {
    return statSync(path).isFile();
  } catch {
    return false;
  }
}

function shown(command) {
  return isAbsolute(command) ? rel(command) : command;
}

function run(command, args, { capture = false, quiet = false } = {}) {
  if (!quiet) console.log(`$ ${shown(command)} ${args.join(' ')}`);
  const result = spawnSync(command, args, {
    cwd: ROOT,
    encoding: 'utf8',
    maxBuffer: 64 << 20,
    stdio: capture ? ['ignore', 'pipe', 'pipe'] : ['ignore', 'inherit', 'inherit'],
  });
  if (result.error) fail(`cannot run ${shown(command)}: ${result.error.message}`);
  return result;
}

function runChecked(command, args, options = {}) {
  const result = run(command, args, options);
  if (result.status !== 0) {
    if (options.capture) process.stderr.write(result.stdout + result.stderr);
    fail(`${shown(command)} failed with exit code ${result.status}`);
  }
  return result;
}

function memoryHelperHint(output) {
  const helpers = new RegExp(
    `(?:^|[^A-Za-z0-9_])_*(?:${MEMORY_HELPERS.join('|')})(?![A-Za-z0-9_])`, 'm');
  return helpers.test(output) ? `\n${MEMORY_HELPER_ADVICE}` : '';
}

// Runs a link step and returns its combined output. On failure it prints the
// output and explains unresolved memory helpers; `echo` filters what is shown
// on success.
function link(command, args, { echo = () => true } = {}) {
  const result = run(command, args, { capture: true });
  const output = `${result.stdout}${result.stderr}`;
  if (result.status !== 0) {
    process.stderr.write(output);
    fail(`${shown(command)} failed with exit code ${result.status}${memoryHelperHint(output)}`);
  }
  const lines = output.split(/\r?\n/).filter((line) => line.trim() && echo(line));
  if (lines.length) console.log(lines.join('\n'));
  return output;
}

// ---------------------------------------------------------------- toolchain

// PATH directories inside the active pixi environment.
function environmentPath() {
  const prefix = process.env.CONDA_PREFIX;
  if (!prefix) {
    fail('this build must run inside a pixi environment, but CONDA_PREFIX is not set. Run it ' +
      'through pixi, e.g. `pixi run -e wasm test-wasm` or `pixi run -e macos test-native`.');
  }
  const root = resolve(prefix);
  return (process.env.PATH || '').split(delimiter).filter(Boolean).map((dir) => resolve(dir))
    .filter((dir) => {
      const inside = relative(root, dir);
      return inside === '' || (!inside.startsWith('..') && !isAbsolute(inside));
    });
}

// A tool from the pixi environment; never a system fallback.
function prefixTool(name) {
  const extensions = process.platform === 'win32' ? ['.exe', '.cmd', '.bat'] : [''];
  for (const dir of environmentPath()) {
    for (const extension of extensions) {
      const candidate = join(dir, `${name}${extension}`);
      if (isFile(candidate)) return candidate;
    }
  }
  return fail(`${name} is missing from the pixi environment ${process.env.CONDA_PREFIX}; ` +
    'refusing to fall back to a system tool. Use an environment that provides it (wasm/js for ' +
    'WebAssembly, macos/linux for native builds).');
}

let clangCache = null;

// pixi's clang 20.1.4+ (<21), or a BuildError.
export function clangTool() {
  if (clangCache) return clangCache;
  const clang = prefixTool('clang');
  const version = runChecked(clang, ['--version'], { capture: true, quiet: true }).stdout;
  const match = /clang version (\d+)\.(\d+)\.(\d+)/.exec(version);
  const [major, minor, patch] = match ? match.slice(1).map(Number) : [0, 0, 0];
  if (major !== 20 || minor < 1 || (minor === 1 && patch < 4)) {
    fail(`${rel(clang)} is not Clang >=20.1.4,<21:\n${version}`);
  }
  console.log(`clang: ${rel(clang)} (${match[0]})`);
  clangCache = clang;
  return clang;
}

function wasmLinker() {
  prefixTool('wasm-ld');
}

// ---------------------------------------------------------------- sources

export function parseSuites(args) {
  const selected = new Set();
  const rest = [];
  let explicit = false;
  for (let i = 0; i < args.length; i++) {
    let value = null;
    if (args[i] === '--suite') {
      if (i + 1 >= args.length) fail('--suite needs a value');
      value = args[++i];
    } else if (args[i].startsWith('--suite=')) {
      value = args[i].slice('--suite='.length);
    } else {
      rest.push(args[i]);
      continue;
    }
    explicit = true;
    for (const name of value.split(',')) {
      if (name === 'all') SUITE_NAMES.forEach((suite) => selected.add(suite));
      else if (SUITES[name]) selected.add(name);
      else fail(`unknown suite "${name}" (known: ${SUITE_NAMES.join(', ')}, all)`);
    }
  }
  const all = !explicit || selected.size === SUITE_NAMES.length;
  const suites = explicit ? SUITE_NAMES.filter((suite) => selected.has(suite)) : [...SUITE_NAMES];
  return { suites, all, explicit, rest };
}

function projectSources(suites) {
  const files = new Set();
  for (const suite of suites) {
    SUITES[suite].sources.forEach((file) => files.add(file));
    files.add(SUITES[suite].test);
  }
  const missing = [...files].filter((file) => !existsSync(join(ROOT, file)));
  if (missing.length) {
    fail(`cannot build suite(s) ${suites.join(', ')}: missing ${missing.join(', ')}.\n` +
      'Build a subset that exists, e.g. --suite runtime.');
  }
  return [...files];
}

// Blanks comments (keeping line breaks) and, with `strings`, the contents of
// string and character literals.
export function stripComments(text, { strings = false } = {}) {
  return text.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'/g,
    (token) => {
      if (token.startsWith('/')) return token.replace(/[^\n]/g, ' ');
      return strings ? `${token[0]}${token[0]}` : token;
    });
}

// Blanks preprocessor directives, including continuation lines.
function blankDirectives(text) {
  return text.replace(/^[ \t]*#(?:[^\n]*\\\r?\n)*[^\n]*/gm,
    (directive) => directive.replace(/[^\n]/g, ' '));
}

// Project sources and the project headers they include must stay
// freestanding: only corec headers in <...>, no __int128, and no C library
// memory functions (memset(...) etc.: call corec's base_mem* routines). Only
// COMPILER_MEM_SOURCE defines the compiler's memory helpers.
export function checkSourcePolicy(files) {
  const memoryCall = new RegExp(`(?<![A-Za-z0-9_])(${MEMORY_HELPERS.join('|')})\\s*\\(`);
  const seen = new Set();
  const queue = [...files];
  const problems = [];
  while (queue.length) {
    const file = queue.pop();
    if (seen.has(file)) continue;
    seen.add(file);
    const source = readFileSync(join(ROOT, file), 'utf8');
    const code = stripComments(source, { strings: true });
    if (/\b__int128\b|\b__uint128_t\b|\b__int128_t\b/.test(code)) {
      problems.push(`${file}: uses a 128-bit integer type`);
    }
    const call = file === COMPILER_MEM_SOURCE ? null : memoryCall.exec(code);
    if (call) {
      problems.push(`${file}: uses the C library memory function ${call[1]}() ` +
        "(call corec's base_mem* routines instead)");
    }
    for (const match of stripComments(source).matchAll(/^\s*#\s*include\s*([<"])([^>"]+)[>"]/gm)) {
      const [, kind, name] = match;
      if (kind === '<') {
        if (!/^(base|platform)\//.test(name)) problems.push(`${file}: system header <${name}>`);
        continue;
      }
      const local = [dirname(file), ...PROJECT_INCLUDES].map((dir) => join(dir, name))
        .find((candidate) => existsSync(join(ROOT, candidate)));
      if (local) queue.push(relative(ROOT, join(ROOT, local)));
    }
  }
  if (problems.length) fail(`freestanding source policy violated:\n  ${problems.join('\n  ')}`);
}

function suiteDefines(suites) {
  return suites.map((suite) => `MS_TEST_SUITE_${suite.toUpperCase()}=1`);
}

// build/<kind>/tests-<id>: the build directory of one suite set (see "Build
// identity" above), independent of the order suites were named in.
export function testBuildDir(kind, suites) {
  const selected = SUITE_NAMES.filter((suite) => suites.includes(suite));
  if (selected.length === 0) fail('no test suite selected');
  const id = selected.length === SUITE_NAMES.length ? 'all' : selected.join('-');
  return join(ROOT, 'build', kind, `tests-${id}`);
}

function objectPath(objDir, file, extension) {
  return join(objDir, `${file.replace(/[\\/]/g, '_').replace(/\.c$/, '')}${extension}`);
}

// ---------------------------------------------------------------- native builds

function clangCompile(clang, objDir, files, { flags = [], defines = [], project = true } = {}) {
  mkdirSync(objDir, { recursive: true });
  const includes = project
    ? [...PROJECT_INCLUDES.flatMap((dir) => ['-I', dir]),
      ...COREC_INCLUDES.flatMap((dir) => ['-isystem', dir])]
    : COREC_INCLUDES.flatMap((dir) => ['-I', dir]);
  return files.map((file) => {
    const object = objectPath(objDir, file, '.o');
    runChecked(clang, [...flags, ...FREESTANDING, ...(project ? WARNINGS : []),
      ...defines.map((define) => `-D${define}`), ...includes, '-c', file, '-o', rel(object)]);
    return object;
  });
}

function nativePlatform() {
  if (process.platform === 'darwin') return { file: `${COREC}/platform/platform_macos.c`, os: 'macos' };
  if (process.platform === 'linux') return { file: `${COREC}/platform/platform_linux.c`, os: 'linux' };
  if (process.platform === 'win32') return { file: `${COREC}/platform/platform_windows.c`, os: 'windows' };
  return fail(`unsupported host platform ${process.platform}`);
}

// The OS calls corec's platform_macos.c declares (`extern ... name(`), as
// Mach-O symbol names.
function macosPlatformImports() {
  const source = readFileSync(join(ROOT, COREC, 'platform', 'platform_macos.c'), 'utf8');
  const names = [...stripComments(source).matchAll(/^[ \t]*extern\b[^;(]*?\b([A-Za-z_]\w*)\s*\(/gm)]
    .map((match) => `_${match[1]}`);
  if (names.length === 0) fail('cannot find the OS calls declared by corec platform_macos.c');
  return names;
}

export function auditMacosBinary(binary) {
  const libraries = runChecked(prefixTool('otool'), ['-L', binary], { capture: true, quiet: true })
    .stdout.split('\n').slice(1).map((line) => line.trim().split(' ')[0]).filter(Boolean);
  if (libraries.length !== 1 || libraries[0] !== '/usr/lib/libSystem.B.dylib') {
    fail(`${rel(binary)} must link only libSystem, links: ${libraries.join(', ')}`);
  }
  const imports = runChecked(prefixTool('nm'), ['-u', binary], { capture: true, quiet: true })
    .stdout.split('\n').map((line) => line.trim()).filter(Boolean);
  const helpers = imports.filter((symbol) => MEMORY_HELPERS.includes(symbol.replace(/^_+/, '')));
  if (helpers.length) {
    fail(`${rel(binary)} imports compiler-generated memory helper(s) ${helpers.join(', ')} ` +
      `from libSystem (libc).\n${MEMORY_HELPER_ADVICE}`);
  }
  const allowed = new Set([...macosPlatformImports(), ...MACOS_STACK_PROTECTOR]);
  const unexpected = imports.filter((symbol) => !allowed.has(symbol));
  if (unexpected.length) {
    fail(`${rel(binary)} imports libSystem symbols outside corec's platform layer: ` +
      `${unexpected.join(', ')}`);
  }
  console.log(`audit: ${rel(binary)} links only libSystem; imports only corec platform calls ` +
    `and stack-protector hooks: ${imports.join(' ')}`);
}

// GNU ld and lld print `object.o: reference to sym` / `definition of sym`
// for every --trace-symbol.
const TRACE_LINE = /(\S+\.o):\s+(reference to|definition of)\s+([A-Za-z_]\w*)/;

export function auditLinuxLink(binary, output) {
  const traces = output.split(/\r?\n/).map((line) => TRACE_LINE.exec(line)).filter(Boolean)
    .map(([, object, kind, symbol]) => ({ object: basename(object), kind, symbol }));
  if (traces.length === 0) {
    fail(`the linker printed no --trace-symbol output for ${rel(binary)}, so memory helper ` +
      "references cannot be audited (corec's platform_linux.c always defines memcpy/memset)");
  }
  const references = traces.filter((trace) => trace.kind === 'reference to');
  const project = references.filter((trace) => !trace.object.startsWith('third_party_corec_'));
  if (project.length) {
    fail('project object(s) reference compiler-generated memory helpers: ' +
      `${project.map((trace) => `${trace.object} -> ${trace.symbol}`).join(', ')}\n` +
      MEMORY_HELPER_ADVICE);
  }
  const corec = [...new Set(references.map((trace) => trace.symbol))];
  console.log(`audit: ${rel(binary)} links no library; project objects reference no memory ` +
    'helper' + (corec.length
      ? `; corec base objects use corec's own weak ${corec.join('/')} (platform_linux.c, not libc)`
      : ''));
}

function buildNativeClang(suites, platform) {
  const clang = clangTool();
  const dir = testBuildDir('native', suites);
  const out = join(dir, 'ms_tests');
  const objDir = join(dir, 'obj');
  const project = [...projectSources(suites), RUNNER];
  checkSourcePolicy(project);
  const objects = [
    // corec's own files with corec's native flags.
    ...clangCompile(clang, objDir, [...COREC_BASE, platform.file], { flags: ['-g'], project: false }),
    ...clangCompile(clang, objDir, project, { flags: ['-O2', '-g'], defines: suiteDefines(suites) }),
  ];
  if (platform.os === 'macos') {
    link(clang, ['-nostdlib', '-g', ...objects.map(rel), '-lSystem', '-Wl,-e,__start',
      '-o', rel(out)]);
    auditMacosBinary(out);
  } else {
    const traces = MEMORY_HELPERS.map((name) => `-Wl,--trace-symbol=${name}`);
    const output = link(clang, ['-nostdlib', '-g', ...objects.map(rel), ...traces, '-o', rel(out)],
      { echo: (line) => !TRACE_LINE.test(line) });
    auditLinuxLink(out, output);
  }
  return out;
}

// MSVC tools of the vcvars64 environment, from the MSVC directory itself so
// that no other `link` on PATH can shadow them.
function msvcTool(name) {
  const tools = process.env.VCToolsInstallDir;
  if (!tools) {
    fail('MSVC is not set up (VCToolsInstallDir is unset): run vcvars64.bat first, as corec CI does.');
  }
  const tool = join(tools, 'bin', 'Hostx64', 'x64', `${name}.exe`);
  if (!isFile(tool)) fail(`${tool} is missing: the x64 MSVC tools of vcvars64.bat are required.`);
  return tool;
}

function auditWindowsBinary(binary) {
  const dumpbin = msvcTool('dumpbin');
  const listing = runChecked(dumpbin, ['/nologo', '/dependents', binary],
    { capture: true, quiet: true }).stdout;
  const dlls = listing.split(/\r?\n/).map((line) => line.trim())
    .filter((line) => /^[\w.-]+\.dll$/i.test(line)).map((name) => name.toLowerCase());
  if (!dlls.includes('kernel32.dll')) {
    fail(`cannot read the DLL dependencies of ${rel(binary)} from dumpbin:\n${listing}`);
  }
  const unexpected = dlls.filter((name) => !WINDOWS_DLLS.includes(name));
  if (unexpected.length) {
    fail(`${rel(binary)} depends on DLLs other than kernel32/shell32: ${unexpected.join(', ')}`);
  }
  const imports = parseDumpbinImports(runChecked(dumpbin, ['/nologo', '/imports', binary],
    { capture: true, quiet: true }).stdout);
  checkWindowsImports(imports, rel(binary));
  console.log(`audit: ${rel(binary)} depends only on ${dlls.join(', ')} (no CRT); none of its ` +
    `${imports.length} imports is a memory helper (${COMPILER_MEM_HELPERS.join('/')}: ` +
    `${COMPILER_MEM_SOURCE})`);
}

function isMemoryHelper(symbol) {
  return MEMORY_HELPERS.includes(symbol.replace(/^_+/, ''));
}

// Imported function names from `dumpbin /imports` (lines "  <hint> <name>"
// before the trailing Summary section).
export function parseDumpbinImports(listing) {
  return listing.split(/^\s*Summary\s*$/m)[0].split(/\r?\n/)
    .map((line) => /^\s+(?:[0-9A-Fa-f]+\s+)?([A-Za-z_]\w*)\s*$/.exec(line)?.[1])
    .filter(Boolean);
}

// corec always imports ExitProcess, which proves the listing was understood;
// a memory helper imported from any DLL is a C library resolution.
export function checkWindowsImports(imports, binary) {
  if (!imports.includes('ExitProcess')) fail(`cannot read the imports of ${binary} from dumpbin`);
  const helpers = imports.filter(isMemoryHelper);
  if (helpers.length) {
    fail(`${binary} imports memory helper(s) ${helpers.join(', ')} from a DLL instead of ` +
      `${COMPILER_MEM_SOURCE}.\n${MEMORY_HELPER_ADVICE}`);
  }
}

// External symbols a COFF object defines and references, from
// `dumpbin /symbols` lines such as
//   009 00000000 SECT3  notype ()    External     | memset
//   00A 00000000 UNDEF  notype ()    External     | base_memset
export function parseCoffSymbols(listing) {
  const defined = new Set();
  const referenced = new Set();
  for (const line of listing.split(/\r?\n/)) {
    const match = /^\s*[0-9A-Fa-f]{3,}\s+[0-9A-Fa-f]{8}\s+(UNDEF|SECT[0-9A-Fa-f]+)\s.*?\b(?:External|WeakExternal)\s+\|\s+(\S+)/
      .exec(line);
    if (match) (match[1] === 'UNDEF' ? referenced : defined).add(match[2]);
  }
  return { defined, referenced };
}

// The adapter must define exactly the helpers it forwards, to corec's
// base_mem* in corec's mem object, and neither object may reference a memory
// helper: an optimizer turning base_memset's loop into a memset call (as /O2
// does) would make memset call itself forever.
export function checkMemoryAdapters(adapter, mem, names = { adapter: 'adapter', mem: 'mem' }) {
  const forwarded = COMPILER_MEM_HELPERS.map((helper) => `base_${helper}`);
  if (!COMPILER_MEM_HELPERS.every((helper) => adapter.defined.has(helper)) ||
      !forwarded.every((name) => adapter.referenced.has(name) && mem.defined.has(name))) {
    fail(`${names.adapter} must define ${COMPILER_MEM_HELPERS.join('/')} forwarding to ` +
      `${forwarded.join('/')}, which ${names.mem} must define (or dumpbin's output was not understood)`);
  }
  const recursive = [...new Set([...adapter.referenced, ...mem.referenced])].filter(isMemoryHelper);
  if (recursive.length) {
    fail(`memory helper recursion: ${names.adapter} or ${names.mem} references ${recursive.join(', ')}, ` +
      "so the adapters would call themselves. Compile corec's base/mem.c and the adapter without " +
      '/O flags, as corec does, so that they stay plain loops and forwarding calls.');
  }
}

function auditWindowsAdapters(adapterObject, memObject) {
  const symbols = (object) => parseCoffSymbols(runChecked(msvcTool('dumpbin'),
    ['/nologo', '/symbols', object], { capture: true, quiet: true }).stdout);
  checkMemoryAdapters(symbols(adapterObject), symbols(memObject),
    { adapter: rel(adapterObject), mem: rel(memObject) });
  console.log(`audit: ${rel(adapterObject)} forwards ${COMPILER_MEM_HELPERS.join('/')} to corec ` +
    `base_mem*; neither it nor ${rel(memObject)} references a memory helper (no recursion)`);
}

function buildNativeMsvc(suites, platform) {
  const dir = testBuildDir('native', suites);
  const out = join(dir, 'ms_tests.exe');
  const objDir = join(dir, 'obj');
  const project = [...projectSources(suites), RUNNER];
  checkSourcePolicy([...project, COMPILER_MEM_SOURCE]);
  mkdirSync(objDir, { recursive: true });
  const cl = msvcTool('cl');
  // corec's cl/link flags: no CRT, no default libraries, kernel32/shell32
  // only. Like corec, no /O flags: MSVC's optimizer can turn loops into
  // memset/memcpy calls, which a CRT-free link cannot resolve. Even so,
  // cl/clang-cl emit memset/memcpy for local aggregates: COMPILER_MEM_SOURCE
  // provides exactly those, over corec's base_mem*.
  const common = ['/nologo', '/X', '/std:c11', '/Zc:preprocessor', '/GS-', '/Gs0', '/kernel', '/c'];
  const includes = [...COREC_INCLUDES, ...PROJECT_INCLUDES].map((dir) => `/I${dir}`);
  const defines = suiteDefines(suites).map((define) => `/D${define}`);
  const objects = [...COREC_BASE, platform.file, ...project, COMPILER_MEM_SOURCE].map((file) => {
    const object = objectPath(objDir, file, '.obj');
    runChecked(cl, [...common, ...includes, ...(project.includes(file) ? defines : []), file,
      `/Fo${rel(object)}`]);
    return object;
  });
  auditWindowsAdapters(objectPath(objDir, COMPILER_MEM_SOURCE, '.obj'),
    objectPath(objDir, `${COREC}/base/mem.c`, '.obj'));
  link(msvcTool('link'), ['/nologo', '/subsystem:console', '/nodefaultlib', '/entry:_start',
    'kernel32.lib', 'shell32.lib', ...objects.map(rel), `/out:${rel(out)}`]);
  auditWindowsBinary(out);
  return out;
}

export function buildNative(suites) {
  const platform = nativePlatform();
  return platform.os === 'windows'
    ? buildNativeMsvc(suites, platform)
    : buildNativeClang(suites, platform);
}

// ---------------------------------------------------------------- WebAssembly

// The wasi_snapshot_preview1 functions corec's platform_wasm.c imports.
export function corecWasiImports() {
  const source = readFileSync(join(ROOT, COREC, 'platform', 'platform_wasm.c'), 'utf8');
  const names = new Set([...blankDirectives(stripComments(source)).matchAll(/\bWASI\(\s*([A-Za-z_]\w*)\s*\)/g)]
    .map((match) => match[1]));
  if (names.size === 0) fail('cannot find the WASI imports declared by corec platform_wasm.c');
  return names;
}

// MS_ABI_VERSION from c/engine.h.
export function abiVersion() {
  const match = /^[ \t]*#[ \t]*define[ \t]+MS_ABI_VERSION[ \t]+(\d+)u?\b/m
    .exec(readFileSync(join(ROOT, 'c', 'engine.h'), 'utf8'));
  if (!match) fail('cannot read MS_ABI_VERSION from c/engine.h');
  return Number(match[1]);
}

function wasmBuild({ out, objDir, sources, defines = [], platformDefines = [], optimize, linkFlags }) {
  const clang = clangTool();
  wasmLinker();
  mkdirSync(dirname(out), { recursive: true });
  checkSourcePolicy(sources);
  const target = [...WASM_TARGET, ...optimize];
  const objects = [
    ...clangCompile(clang, objDir, [...COREC_BASE, `${COREC}/platform/platform_wasm.c`],
      { flags: target, defines: platformDefines, project: false }),
    ...clangCompile(clang, objDir, sources, { flags: target, defines: [...defines, ...platformDefines] }),
  ];
  // No --allow-undefined: any unresolved libc or compiler-rt symbol fails here.
  link(clang, [...target, '-nostdlib', ...linkFlags, ...objects.map(rel), '-o', rel(out)]);
  return out;
}

export function buildWasm(suites) {
  const dir = testBuildDir('wasm', suites);
  return wasmBuild({
    out: join(dir, 'ms_tests.wasm'),
    objDir: join(dir, 'obj'),
    sources: [...projectSources(suites), RUNNER],
    defines: suiteDefines(suites),
    optimize: WASM_TEST_OPT,
    linkFlags: WASM_TEST_LINK,
  });
}

// Test-only reactor exercising the reactor conventions, built in `dir`
// (check-wasm passes its private scratch directory).
export function buildSmoke(dir = join(ROOT, 'build', 'wasm')) {
  return wasmBuild({
    out: join(dir, 'reactor_smoke.wasm'),
    objDir: join(dir, 'obj-reactor_smoke'),
    sources: ['c/runtime.c', SMOKE_SOURCE],
    platformDefines: ['PLATFORM_SKIP_ENTRY'],
    optimize: WASM_TEST_OPT,
    linkFlags: WASM_REACTOR_LINK,
  });
}

// Every c/*.c file except the Windows-only compiler memory adapters, once
// the engine sources all exist.
function reactorSources() {
  const missing = [...ENGINE_SOURCES, WASM_API_HEADER].filter((file) => !existsSync(join(ROOT, file)));
  if (missing.length) {
    fail(`the production reactor needs every engine source; missing: ${missing.join(', ')}.\n` +
      'Nothing was built: no placeholder module is ever produced.');
  }
  return readdirSync(join(ROOT, 'c')).filter((name) => name.endsWith('.c')).sort()
    .map((name) => `c/${name}`).filter((file) => file !== COMPILER_MEM_SOURCE);
}

// Export names declared by the reactor ABI sources: export_name("...")
// attributes and uses of macros whose expansion is export_name(#param),
// outside preprocessor directives. Every name must start with ms_ and appear
// in the header (the documented ABI). Returns { names, problems }.
export function parseExportDeclarations(headerText, sourceText) {
  const header = stripComments(headerText);
  const source = stripComments(sourceText);
  const macros = new Set();
  for (const text of [header, source]) {
    for (const match of text.matchAll(
      /^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)\(\s*([A-Za-z_]\w*)\s*\)((?:[^\n]*\\\r?\n)*[^\n]*)/gm)) {
      if (new RegExp(`export_name\\s*\\(\\s*#\\s*${match[2]}\\s*\\)`).test(match[3])) macros.add(match[1]);
    }
  }
  const names = new Set();
  const problems = [];
  for (const text of [header, source].map(blankDirectives)) {
    for (const macro of macros) {
      for (const match of text.matchAll(new RegExp(`\\b${macro}\\s*\\(\\s*([^)\\s]*)\\s*\\)`, 'g'))) {
        names.add(match[1]);
      }
    }
    for (const match of text.matchAll(/export_name\s*\(\s*"([^"]*)"\s*\)/g)) names.add(match[1]);
  }
  const headerCode = blankDirectives(header);
  for (const name of names) {
    if (!/^ms_[a-z0-9_]+$/.test(name)) problems.push(`export "${name}" does not match ms_[a-z0-9_]+`);
    else if (!new RegExp(`\\b${name}\\b`).test(headerCode)) {
      problems.push(`export ${name} is not declared in ${WASM_API_HEADER}`);
    }
  }
  for (const required of ['ms_abi_version', 'ms_init']) {
    if (!names.has(required)) problems.push(`required export ${required} (engine.h) is not declared`);
  }
  return { names: [...names].sort(), problems };
}

// The production export list, after checking that no other engine source
// declares exports.
export function productionExports() {
  const problems = [];
  for (const name of readdirSync(join(ROOT, 'c')).filter((file) => /\.[ch]$/.test(file))) {
    const file = `c/${name}`;
    if (file === WASM_API_HEADER || file === WASM_API_SOURCE) continue;
    if (/export_name/.test(stripComments(readFileSync(join(ROOT, file), 'utf8')))) {
      problems.push(`${file}: exports belong in ${WASM_API_SOURCE} only`);
    }
  }
  const parsed = parseExportDeclarations(readFileSync(join(ROOT, WASM_API_HEADER), 'utf8'),
    readFileSync(join(ROOT, WASM_API_SOURCE), 'utf8'));
  problems.push(...parsed.problems);
  for (const name of PLANNER_EXPORTS) {
    if (!parsed.names.includes(name)) problems.push(`required export ${name} (move advisor) is not declared`);
  }
  if (problems.length) fail(`reactor export declarations:\n  ${problems.join('\n  ')}`);
  return parsed.names;
}

// Checks a module's imports and exports against explicit allowlists: imports
// are corec's WASI functions plus `hostImports`; exports are exactly memory,
// __heap_base and the given functions. Returns the compiled module.
export function auditWasmModule(path, { functions, hostImports = new Set(), requireHost = [] }) {
  const module = new WebAssembly.Module(readFileSync(path));
  const wasi = corecWasiImports();
  const problems = [];
  const imports = WebAssembly.Module.imports(module).map((entry) => ({
    ...entry, qualified: `${entry.module}.${entry.name}`,
  }));
  for (const { module: from, name, kind, qualified } of imports) {
    const allowed = kind === 'function' &&
      ((from === 'wasi_snapshot_preview1' && wasi.has(name)) || hostImports.has(qualified));
    if (!allowed) {
      problems.push(`import ${qualified} (${kind}) is outside the allowlist; unresolved libc or ` +
        'compiler-rt symbols must fail the link instead');
    }
  }
  for (const name of requireHost) {
    if (!imports.some((entry) => entry.qualified === name)) problems.push(`missing host import ${name}`);
  }
  const exports = WebAssembly.Module.exports(module);
  const expected = new Map([['memory', 'memory'], ['__heap_base', 'global'],
    ...functions.map((name) => [name, 'function'])]);
  for (const { name, kind } of exports) {
    if (!expected.has(name)) problems.push(`unexpected export ${name} (${kind})`);
    else if (expected.get(name) !== kind) problems.push(`export ${name} is a ${kind}, not a ${expected.get(name)}`);
  }
  for (const [name, kind] of expected) {
    if (!exports.some((entry) => entry.name === name)) problems.push(`missing export ${name} (${kind})`);
  }
  if (problems.length) fail(`${rel(path)} failed the import/export audit:\n  ${problems.join('\n  ')}`);
  console.log(`audit: ${rel(path)}\n  imports: ${imports.map((entry) => entry.qualified).join(' ') || '(none)'}` +
    `\n  exports: ${exports.map((entry) => entry.name).join(' ')}`);
  return module;
}

export function auditProduction(path) {
  return auditWasmModule(path, {
    functions: [...productionExports(), ...COREC_WASM_EXPORTS],
    hostImports: HOST_IMPORTS,
  });
}

// build/wasm/minesweeper.wasm: the production reactor (both the game and the
// solver instance), audited before it is returned.
export function buildReactor() {
  const sources = reactorSources();
  productionExports();
  const out = wasmBuild({
    out: join(ROOT, 'build', 'wasm', REACTOR_FILE),
    objDir: join(ROOT, 'build', 'wasm', 'obj-minesweeper'),
    sources,
    platformDefines: ['PLATFORM_SKIP_ENTRY'],
    optimize: WASM_REACTOR_OPT,
    linkFlags: WASM_REACTOR_LINK,
  });
  auditProduction(out);
  return out;
}

// Links `source` exactly like a reactor into `out`; returns the spawn result
// instead of failing, so callers can prove that undefined symbols are
// rejected.
export function tryLinkWasm(source, out) {
  const clang = clangTool();
  wasmLinker();
  mkdirSync(dirname(out), { recursive: true });
  return run(clang, [...WASM_TARGET, ...WASM_TEST_OPT, ...FREESTANDING,
    ...COREC_INCLUDES.flatMap((dir) => ['-I', dir]), ...WASM_REACTOR_LINK, source, '-o', rel(out)],
  { capture: true });
}

// ---------------------------------------------------------------- static site

// URLs that load nothing from the site.
const NON_FILE_URL = /^(?:#|data:|blob:|about:|mailto:|tel:|javascript:)/i;
const EXTERNAL_URL = /^(?:[a-z][a-z0-9+.-]*:|\/\/)/i;

// A problem with `url`, referenced from `from` inside `site`, or null.
// Navigation links may be external; nothing may be an absolute path, which
// would break hosting under a subpath.
function urlProblem(site, from, url, { external = false, module = false } = {}) {
  const where = `${rel(from)}: '${url}'`;
  if (url === '' || NON_FILE_URL.test(url)) return null;
  if (EXTERNAL_URL.test(url)) return external ? null : `${where} is external (the site must be self-contained)`;
  if (url.startsWith('/')) return `${where} is an absolute path (breaks hosting under a subpath)`;
  if (module && !/^\.\.?\//.test(url)) return `${where} is a bare module specifier (use './...')`;
  const path = url.split(/[?#]/)[0];
  if (path === '') return null;
  let decoded;
  try {
    decoded = decodeURIComponent(path);
  } catch {
    return `${where} is not a valid URL`;
  }
  const target = resolve(dirname(from), ...decoded.split('/'));
  const inside = relative(site, target);
  if (inside === '..' || inside.startsWith(`..${sep}`) || isAbsolute(inside)) {
    return `${where} points outside the site`;
  }
  if (isFile(target) || isFile(join(target, 'index.html'))) return null;
  return `${where} does not exist in the site`;
}

// Static module graph edges and fetched URLs given as string literals. The
// patterns run on code whose string contents are blanked (so text inside
// strings never matches), and the literal is read back at the same offsets.
const JS_MODULE_PATTERNS = [
  /\bimport\s*(?:[\w$*{}\s,]+?\s*from\s*)?(['"])([^'"\n]*)\1/dg,
  /\bexport\s*(?:\*\s*(?:as\s+[\w$]+\s*)?|\{[^}]*\}\s*)from\s*(['"])([^'"\n]*)\1/dg,
  /\bimport\s*\(\s*(['"])([^'"\n]*)\1\s*[,)]/dg,
];
const JS_URL_PATTERNS = [
  /\bnew\s+URL\s*\(\s*(['"])([^'"\n]*)\1\s*,\s*import\.meta\.url\s*\)/dg,
  /\bnew\s+(?:Shared)?Worker\s*\(\s*(['"])([^'"\n]*)\1/dg,
  /\bimportScripts\s*\(\s*(['"])([^'"\n]*)\1/dg,
  /\bfetch\s*\(\s*(['"])([^'"\n]*)\1/dg,
];

// Same-length copy of `code` with the contents of string and template
// literals replaced by spaces.
function blankStringContents(code) {
  return code.replace(/"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|`(?:\\[\s\S]|[^`\\])*`/g,
    (literal) => `${literal[0]}${' '.repeat(literal.length - 2)}${literal[literal.length - 1]}`);
}

function checkJs(site, file, text, problems) {
  const code = stripComments(text);
  const masked = blankStringContents(code);
  const scan = (patterns, options) => {
    for (const pattern of patterns) {
      for (const match of masked.matchAll(pattern)) {
        const [start, end] = match.indices[2];
        problems.push(urlProblem(site, file, code.slice(start, end), options));
      }
    }
  };
  scan(JS_MODULE_PATTERNS, { module: true });
  scan(JS_URL_PATTERNS, {});
}

function checkCss(site, file, text, problems) {
  const css = text.replace(/\/\*[\s\S]*?\*\//g, ' ');
  for (const match of css.matchAll(/url\(\s*(?:"([^"]*)"|'([^']*)'|([^)\s]*))\s*\)/gi)) {
    problems.push(urlProblem(site, file, (match[1] ?? match[2] ?? match[3]).trim()));
  }
  for (const match of css.matchAll(/@import\s+(?:"([^"]*)"|'([^']*)')/gi)) {
    problems.push(urlProblem(site, file, match[1] ?? match[2]));
  }
}

function checkHtml(site, file, problems) {
  const html = readFileSync(file, 'utf8').replace(/<!--[\s\S]*?-->/g, ' ');
  for (const tag of html.matchAll(/<([A-Za-z][\w:-]*)\b([^>]*)>/g)) {
    const name = tag[1].toLowerCase();
    for (const attribute of tag[2].matchAll(
      /([^\s"'=<>/]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>`]+))/g)) {
      const key = attribute[1].toLowerCase();
      const value = (attribute[2] ?? attribute[3] ?? attribute[4]).trim();
      if (name === 'base' && key === 'href') {
        problems.push(`${rel(file)}: <base href> changes how every relative URL resolves`);
        continue;
      }
      const navigation = (name === 'a' || name === 'area') && key === 'href';
      const resource = key === 'src' || key === 'poster' || (name === 'object' && key === 'data') ||
        (!navigation && (key === 'href' || key === 'xlink:href'));
      if (navigation || resource) problems.push(urlProblem(site, file, value, { external: navigation }));
    }
  }
  for (const script of html.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi)) {
    if (!/\bsrc\s*=/i.test(script[1])) checkJs(site, file, script[2], problems);
  }
  for (const style of html.matchAll(/<style\b[^>]*>([\s\S]*?)<\/style\s*>/gi)) {
    checkCss(site, file, style[1], problems);
  }
}

function siteFiles(dir) {
  return readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const path = join(dir, entry.name);
    return entry.isDirectory() ? siteFiles(path) : [path];
  });
}

// Static reference check of a site directory: every resource an HTML, CSS
// or JS file loads by a literal URL is relative and exists, nothing is
// external, absolute or a bare module specifier. Returns problem strings.
export function checkSiteReferences(site) {
  const problems = [];
  for (const file of siteFiles(site)) {
    const extension = extname(file).toLowerCase();
    if (extension === '.html' || extension === '.htm') checkHtml(site, file, problems);
    else if (extension === '.css') checkCss(site, file, readFileSync(file, 'utf8'), problems);
    else if (extension === '.js' || extension === '.mjs') checkJs(site, file, readFileSync(file, 'utf8'), problems);
  }
  return problems.filter(Boolean);
}

// Copies every non-dot file under `source`, keeping relative paths.
function copyTree(source, target, reserved, prefix = '') {
  const copied = [];
  for (const entry of readdirSync(source, { withFileTypes: true })) {
    if (entry.name.startsWith('.')) continue;
    const from = join(source, entry.name);
    const path = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isDirectory()) {
      copied.push(...copyTree(from, join(target, entry.name), reserved, path));
    } else if (entry.isFile()) {
      if (reserved.has(path)) fail(`${STATIC_DIR}/${path} collides with a generated ${DIST_DIR}/ file`);
      mkdirSync(target, { recursive: true });
      copyFileSync(from, join(target, entry.name));
      copied.push(path);
    } else {
      fail(`${STATIC_DIR}/${path} is not a regular file or directory`);
    }
  }
  return copied;
}

// The release file set: dist/ holds exactly STATIC_FILES, the reactor and
// VENDOR_FILES as regular files, and nothing else (no stray source, test,
// dotfile or link). Returns problem strings.
export function checkSiteFiles(site) {
  const expected = [...STATIC_FILES, REACTOR_FILE, ...Object.keys(VENDOR_FILES)];
  const present = [];
  const problems = [];
  const walk = (dir) => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const path = join(dir, entry.name);
      const name = relative(site, path).split(sep).join('/');
      if (entry.isDirectory()) walk(path);
      else if (!entry.isFile()) problems.push(`${DIST_DIR}/${name} is not a regular file`);
      else if (!expected.includes(name)) {
        problems.push(`unexpected ${DIST_DIR}/${name} (register new site files in ` +
          'scripts/build.mjs STATIC_FILES)');
      } else present.push(name);
    }
  };
  walk(site);
  for (const name of expected) {
    if (!present.includes(name)) problems.push(`missing ${DIST_DIR}/${name}`);
  }
  return problems;
}

export function verifyDist(site, wasm) {
  const problems = checkSiteFiles(site);
  const sameBytes = (a, b) => isFile(a) && isFile(b) && readFileSync(a).equals(readFileSync(b));
  if (isFile(join(site, REACTOR_FILE)) && !sameBytes(wasm, join(site, REACTOR_FILE))) {
    problems.push(`${REACTOR_FILE} differs from ${rel(wasm)}`);
  }
  for (const [target, source] of Object.entries(VENDOR_FILES)) {
    if (isFile(join(site, target)) && !sameBytes(join(ROOT, source), join(site, target))) {
      problems.push(`${target} differs from ${source}`);
    }
  }
  problems.push(...checkSiteReferences(site));
  if (problems.length) fail(`${DIST_DIR}/ failed verification:\n  ${problems.join('\n  ')}`);
}

// dist/: the deployable static site, recreated from scratch.
export function buildDist() {
  const wasm = buildReactor();
  if (!isFile(join(ROOT, STATIC_DIR, 'index.html'))) fail(`${STATIC_DIR}/index.html is missing`);
  const site = join(ROOT, DIST_DIR);
  rmSync(site, { recursive: true, force: true });
  const copied = copyTree(join(ROOT, STATIC_DIR), site, new Set([REACTOR_FILE, ...Object.keys(VENDOR_FILES)]));
  copyFileSync(wasm, join(site, REACTOR_FILE));
  for (const [target, source] of Object.entries(VENDOR_FILES)) {
    mkdirSync(dirname(join(site, target)), { recursive: true });
    copyFileSync(join(ROOT, source), join(site, target));
  }
  verifyDist(site, wasm);
  console.log(`dist: ${rel(site)}/ verified: ${copied.length} file(s) from ${STATIC_DIR}/, ` +
    `${REACTOR_FILE}, ${Object.keys(VENDOR_FILES).join(', ')}`);
  return site;
}

// ---------------------------------------------------------------- runners

function runnerArgs(selection) {
  return selection.all ? [] : ['--suite', selection.suites.join(',')];
}

function execute(command, args) {
  const result = run(command, args);
  if (result.signal) fail(`${shown(command)} terminated by ${result.signal}`);
  return result.status;
}

const USAGE = 'usage: node scripts/build.mjs native|wasm|test-native|test-wasm|test-node|smoke|' +
  'reactor|dist [--suite NAME[,NAME...]]';

function main(argv) {
  const [action, ...args] = argv;
  const selection = parseSuites(args);
  if (selection.rest.length) fail(`unexpected argument(s): ${selection.rest.join(' ')}\n${USAGE}`);
  const noSuites = () => {
    if (selection.explicit) fail(`${action} builds no test suites; --suite does not apply`);
  };
  switch (action) {
    case 'native':
      console.log(`test runner: ${rel(buildNative(selection.suites))}`);
      return 0;
    case 'wasm':
      console.log(`test runner: ${rel(buildWasm(selection.suites))}`);
      return 0;
    case 'test-native':
      return execute(buildNative(selection.suites), runnerArgs(selection));
    case 'test-wasm': {
      const wasm = buildWasm(selection.suites);
      return execute(prefixTool('wasmtime'), ['--dir', '.', rel(wasm), ...runnerArgs(selection)]);
    }
    case 'test-node': {
      const wasm = buildWasm(selection.suites);
      return execute(process.execPath,
        [`${COREC}/examples/js/run_node.js`, rel(wasm), ...runnerArgs(selection)]);
    }
    case 'smoke':
      noSuites();
      buildSmoke();
      return 0;
    case 'reactor':
      noSuites();
      buildReactor();
      return 0;
    case 'dist':
      noSuites();
      buildDist();
      return 0;
    default:
      return fail(USAGE);
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    process.exitCode = main(process.argv.slice(2));
  } catch (error) {
    if (!(error instanceof BuildError)) throw error;
    console.error(`build.mjs: ${error.message}`);
    process.exitCode = 2;
  }
}
