import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';
import {
  CSP_META, PRE_PAINT_TAG, MANIFEST_LINK, FAVICON_HREF,
  PROJECTS_WORKER_URL, IMAGE_WORKER_URL, WASM_IMPORT, OPEN_IN_CONFIG_URL, NO_SIBLINGS,
} from './tools/singleFilePatterns.js';

// `npm run build` only — the app itself stays a no-build ES-module site: this folds the module graph, the
// CSS and the icons into one self-contained stencil.html, inline so vite stays the single dev dependency.

const root = dirname(fileURLToPath(import.meta.url));
const OUT_DIR = process.env.STENCIL_SINGLEFILE_OUTDIR
  || resolve(root, 'node_modules/.stencil-singlefile');

const dataUri = file =>
  'data:image/svg+xml,' + encodeURIComponent(readFileSync(resolve(root, file), 'utf8'));

// The raw HTML is rewritten before vite:build-html extracts assets out of it: the pre-paint script goes
// inline, the icons become data: URIs, and the PWA manifest link and <meta> CSP go.
const prepareHtml = () => ({
  name: 'stencil-singlefile-html',
  enforce: 'pre',
  transform(code, id) {
    if (!id.endsWith('index.html')) return null;
    const prePaint = readFileSync(resolve(root, 'js/prePaintTheme.js'), 'utf8');
    const out = code
      .replace(CSP_META, '')
      .replace(MANIFEST_LINK, '')
      .replace(PRE_PAINT_TAG, `<script>\n${prePaint}\n</script>`)
      .replace(FAVICON_HREF, `href="${dataUri('favicon.svg')}"`);
    // Better a failed build than a "single" file that quietly needs siblings once index.html moves on.
    for (const stale of NO_SIBLINGS) {
      if (out.includes(stale)) this.error(`index.html still references ${stale} after the single-file rewrite`);
    }
    return out;
  },
});

// Four siblings cannot come along, so their loaders fail fast into the fallbacks the app already has
// rather than a doomed file:// request: projectsWorker, imageWorker, wasm/stencilCore, openInConfig.json.
const useFallbackPaths = () => ({
  name: 'stencil-singlefile-fallbacks',
  enforce: 'pre',
  transform(code, id) {
    if (id.endsWith('core/launch/tabsCoordinator.js')) {
      return code.replace(
        PROJECTS_WORKER_URL,
        "(() => { throw new Error('single-file build: no module worker'); })()"
      );
    }
    if (id.endsWith('worker/imageTasks.js')) {
      return code.replace(
        IMAGE_WORKER_URL,
        "(() => { throw new Error('single-file build: no module worker'); })()"
      );
    }
    if (id.endsWith('core/stencilCore.js')) {
      return code.replace(
        WASM_IMPORT,
        "Promise.reject(new Error('single-file build: wasm core not bundled'))"
      );
    }
    if (id.endsWith('config/openInConfig.js')) {
      return code.replace(
        OPEN_IN_CONFIG_URL,
        "(() => { throw new Error('single-file build: operator config not bundled'); })()"
      );
    }
    return null;
  },
});

// Folded back in at writeBundle, not generateBundle, so Vite has finished its own chunk rewrites; the
// redundant app.js / app.css are left in staging for tools/buildHtml.js to delete.
const inlineEverything = () => ({
  name: 'stencil-singlefile-inline',
  enforce: 'post',
  writeBundle(opts, bundle) {
    const html = Object.values(bundle).find(f => f.fileName.endsWith('.html'));
    if (!html) this.error('no HTML emitted');
    let out = html.source;
    for (const file of Object.values(bundle)) {
      if (file === html) continue;
      if (file.type === 'chunk' && file.fileName.endsWith('.js')) {
        // `</script` in a string/regex/comment would close the tag early; `<\/` is the
        // same character sequence to JS and inert to the HTML parser.
        const code = file.code.replace(/<\/(script)/gi, '<\\/$1');
        out = out.replace(
          new RegExp(`<script[^>]*src="[^"]*${file.fileName}"[^>]*></script>`),
          () => `<script type="module">\n${code}\n</script>`
        );
      } else if (file.fileName.endsWith('.css')) {
        if (/<\/style/i.test(file.source)) this.error('CSS contains `</style`, which cannot be inlined');
        out = out.replace(
          new RegExp(`<link[^>]*href="[^"]*${file.fileName}"[^>]*>`),
          () => `<style>\n${file.source}\n</style>`
        );
      }
    }
    const left = out.match(/(?:src|href)="(?!data:)[^"]*app\.(?:js|css)"/);
    if (left) this.error(`could not inline ${left[0]} — the emitted tag no longer matches`);
    writeFileSync(resolve(opts.dir, html.fileName), out);
  },
});

export default defineConfig({
  root,
  base: './',
  plugins: [prepareHtml(), useFallbackPaths(), inlineEverything()],
  build: {
    outDir: OUT_DIR,
    emptyOutDir: true,
    cssCodeSplit: false,              // one stylesheet to inline
    assetsInlineLimit: Infinity,      // every asset becomes a data: URI
    modulePreload: false,             // no preload links or helper to strip back out
    reportCompressedSize: false,
    chunkSizeWarningLimit: Infinity,  // one big chunk is the whole point
    rollupOptions: {
      input: resolve(root, 'index.html'),
      output: { codeSplitting: false, entryFileNames: 'app.js', assetFileNames: 'app.[ext]' },
    },
  },
});
