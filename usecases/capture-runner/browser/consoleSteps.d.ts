import type { ShotRunner, ShotStep } from '../lib/shot/runner.js';
import type { BrowserPages } from './pageTools.js';

export function makeConsoleSteps(opts: { runner: ShotRunner; pages: BrowserPages }): readonly ShotStep[];
