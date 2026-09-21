// The rewrite patterns for the single-file build. vite.config.js edits five sources on its way to one
// self-contained HTML: the app shell (inline the pre-paint script and the icons, drop the PWA manifest and the
// <meta> CSP an all-inline file off disk cannot obey) and the three loaders that address a sibling file by URL.
// They live here, dependency-free, so tests/singleFileBuild.test.js can assert they all still match.

export const CSP_META = /^.*<!-- CSP:(?:.*\n)*?.*<meta http-equiv="Content-Security-Policy"[^>]*>\n/m;
export const PRE_PAINT_TAG = /<script src="js\/prePaintTheme\.js"><\/script>/;
export const MANIFEST_LINK = /^.*<link rel="manifest".*\n/m;
export const FAVICON_HREF = /href="favicon\.svg"/g;
export const PROJECTS_WORKER_URL = /new URL\((['"])(?:\.\.\/)+worker\/projectsWorker\.js\1,\s*import\.meta\.url\)/;
export const IMAGE_WORKER_URL = /new URL\((['"])\.\/imageWorker\.js\1,\s*import\.meta\.url\)/;
export const WASM_IMPORT = /import\(WASM_MODULE_PATH\)/;
export const OPEN_IN_CONFIG_URL = /new URL\((['"])\.\/openInConfig\.json\1,\s*import\.meta\.url\)/;

// file → the patterns that must still match inside it.
export const REWRITES = Object.freeze({
  'index.html': [CSP_META, PRE_PAINT_TAG, MANIFEST_LINK, FAVICON_HREF],
  'js/core/launch/tabsCoordinator.js': [PROJECTS_WORKER_URL],
  'js/worker/imageTasks.js': [IMAGE_WORKER_URL],
  'js/core/abi/stencilCore.js': [WASM_IMPORT],
  'js/config/openInConfig.js': [OPEN_IN_CONFIG_URL],
});

// Nothing may point at a sibling once the rewrites have run.
export const NO_SIBLINGS = Object.freeze(['js/prePaintTheme.js', 'favicon.svg', 'rel="manifest"']);
