// Shared harness endpoints. The app is served on a DEDICATED port (not the app's
// conventional 8080) so a developer's stray `npm run serve` can't be silently reused
// by Playwright's webServer, and on 127.0.0.1 explicitly to avoid the macOS
// localhost→::1 vs 127.0.0.1 split that otherwise makes the server look "up" on the
// wrong stack.
export const APP_HOST = process.env.APP_HOST || '127.0.0.1';
export const APP_PORT = Number(process.env.APP_PORT) || 8188;
export const APP_URL = process.env.APP_URL || `http://${APP_HOST}:${APP_PORT}/`;
