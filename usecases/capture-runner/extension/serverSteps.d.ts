import type { ShotStep } from '../lib/shot/runner.js';

export function makeServerSteps(opts: { serverPin: string; localPin: string }): readonly ShotStep[];
