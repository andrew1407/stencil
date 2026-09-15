import type * as pw from 'playwright';

export const playwright: typeof pw;
export const chromium: typeof pw.chromium;
export function e2e(rel: string): Promise<any>;
