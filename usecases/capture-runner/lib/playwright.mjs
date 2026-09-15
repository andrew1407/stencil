// Playwright for the capture scripts, borrowed from e2e/ (no dependency of its own): the
// e2e helpers are imported by file URL so their own `@playwright/test` imports resolve from
// e2e/node_modules.
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
import { REPO } from './paths.mjs';

const require = createRequire(path.join(REPO, 'e2e', 'package.json'));

export const playwright = require('playwright');
export const { chromium } = playwright;

export const e2e = (rel) => import(pathToFileURL(path.join(REPO, 'e2e', rel)).href);
