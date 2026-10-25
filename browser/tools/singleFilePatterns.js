// The rewrite patterns for the single-file build. vite.config.js edits five sources on its way to one
// self-contained HTML: the app shell (inline the pre-paint script and the icons, drop the PWA manifest and the
// <meta> CSP an all-inline file off disk cannot obey) and the four loaders that address a sibling file by URL.
// They live here, dependency-free, so tests/singleFileBuild.test.js can assert they all still match.

export const CSP_META = /^.*<!-- CSP:(?:.*\n)*?.*<meta http-equiv="Content-Security-Policy"[^>]*>\n/m;
export const PRE_PAINT_TAG = /<script src="js\/prePaintTheme\.js"><\/script>/;
export const MANIFEST_LINK = /^.*<link rel="manifest".*\n/m;
export const FAVICON_HREF = /href="\.\.\/common\/icons\/favicon\.svg"/g;
// The generated <link rel="modulepreload"> block (tools/preloadList.js): one file has no siblings to preload.
export const MODULEPRELOAD_BLOCK = /^[ \t]*<!-- modulepreload:start[\s\S]*?<!-- modulepreload:end -->\n/m;
export const PROJECTS_WORKER_URL = /new URL\((['"])(?:\.\.\/)+worker\/projectsWorker\.js\1,\s*import\.meta\.url\)/;
export const IMAGE_WORKER_URL = /new URL\((['"])\.\/imageWorker\.js\1,\s*import\.meta\.url\)/;
export const WASM_IMPORT = /import\(WASM_MODULE_PATH\)/;
export const OPEN_IN_CONFIG_URL = /new URL\((['"])\.\/openInConfig\.json\1,\s*import\.meta\.url\)/;

const fail = (why) => `(() => { throw new Error('single-file build: ${why}'); })()`;

// loader file → [pattern, replacement]: each sibling load fails fast into the fallback the app already has.
export const FALLBACKS = Object.freeze({
  'js/core/launch/tabsCoordinator.js': [PROJECTS_WORKER_URL, fail('no module worker')],
  'js/worker/imageTasks.js': [IMAGE_WORKER_URL, fail('no module worker')],
  'js/core/abi/stencilCore.js': [WASM_IMPORT, "Promise.reject(new Error('single-file build: wasm core not bundled'))"],
  'js/config/openInConfig.js': [OPEN_IN_CONFIG_URL, fail('operator config not bundled')],
});

// file → the patterns that must still match inside it.
export const REWRITES = Object.freeze({
  'index.html': [CSP_META, PRE_PAINT_TAG, MANIFEST_LINK, FAVICON_HREF, MODULEPRELOAD_BLOCK],
  ...Object.fromEntries(Object.entries(FALLBACKS).map(([file, [pattern]]) => [file, [pattern]])),
});

// Nothing may point at a sibling once the rewrites have run: the shell's references, then the loaders' targets.
export const NO_SIBLINGS = Object.freeze(['js/prePaintTheme.js', 'favicon.svg', 'rel="manifest"', 'rel="modulepreload"']);
export const NO_SIBLING_LOADS = Object.freeze([
  'worker/projectsWorker.js', 'imageWorker.js', 'wasm/stencilCore.js', 'openInConfig.json', 'scriptHandles.js',
]);
