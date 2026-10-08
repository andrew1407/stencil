// Shared harness endpoints: a DEDICATED port (not 8080), so a stray `npm run serve` is never
// reused by the webServer, on 127.0.0.1 explicitly, so macOS's localhost→::1 split cannot make
// the server look "up" on the wrong stack.
export const APP_HOST = process.env.APP_HOST || '127.0.0.1';
export const APP_PORT = Number(process.env.APP_PORT) || 8188;
// The server's root is the app (browser/), with common/ at /common/ and the fixtures under /__e2e__/.
export const SITE_URL = process.env.SITE_URL || `http://${APP_HOST}:${APP_PORT}/`;
export const APP_URL = process.env.APP_URL || SITE_URL;
