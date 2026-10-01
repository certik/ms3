// Refuses to test a missing or stale site: dist/ (or $MS_DIST) must hold the
// reactor, corec's WASI host and license (names pinned by scripts/build.mjs)
// and byte-identical copies of every file in static/ (rebuild with
// `node scripts/build.mjs dist`).
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { REACTOR_FILE, ROOT, SITE, VENDOR_PATHS } from './site.mjs';

export default function globalSetup() {
  const problems = [];
  for (const file of ['index.html', REACTOR_FILE, ...VENDOR_PATHS]) {
    if (!existsSync(join(SITE, file))) problems.push(`missing ${file}`);
  }
  for (const name of readdirSync(join(ROOT, 'static'))) {
    const built = join(SITE, name);
    if (!existsSync(built) || !readFileSync(built).equals(readFileSync(join(ROOT, 'static', name)))) {
      problems.push(`${name} differs from static/${name}`);
    }
  }
  if (problems.length) {
    throw new Error(`The site under test (${SITE}) is missing or stale:\n  ${problems.join('\n  ')}\n` +
      'Build it with `node scripts/build.mjs dist` (through pixi), or point MS_DIST at a built site.');
  }
}
