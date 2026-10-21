import type { ShotStep } from '../lib/shot/runner.js';

export function makeSiteSteps(opts: { image: string }): readonly ShotStep[];
