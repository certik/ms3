// Where the browser tests find the site (shared by the config and specs).
// Asset names come from scripts/build.mjs, which pins the dist/ layout.
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { REACTOR_FILE, VENDOR_FILES } from '../../../scripts/build.mjs';

export { REACTOR_FILE };
export const VENDOR_PATHS = Object.keys(VENDOR_FILES);

export const ROOT = fileURLToPath(new URL('../../..', import.meta.url));
export const SITE = resolve(ROOT, process.env.MS_DIST || 'dist');
export const ROOT_PORT = Number(process.env.MS_PORT || 8811);
export const NESTED_PORT = ROOT_PORT + 1;
export const NESTED_BASE = '/projects/minesweeper/';
export const ROOT_URL = `http://127.0.0.1:${ROOT_PORT}/`;
export const NESTED_URL = `http://127.0.0.1:${NESTED_PORT}${NESTED_BASE}`;
