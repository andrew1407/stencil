import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';
import {
  CSP_META, PRE_PAINT_TAG, MANIFEST_LINK, FAVICON_HREF,
  PROJECTS_WORKER_URL, IMAGE_WORKER_URL, WASM_IMPORT, OPEN_IN_CONFIG_URL, NO_SIBLINGS,
} from './tools/singleFilePatterns.js';

// ── Single-file build ───────────────────────────────────────────
// The app itself stays a no-build, native-ES-module site (`npm run serve`). This config
// is only for `npm run build`, which folds the whole module graph, the CSS and the icons
// into ONE self-contained stencil.html that opens straight off disk — no static server.
// Written out inline (no plugin packages) so vite stays the single dev dependency.

const root = dirname(fileURLToPath(import.meta.url));
const OUT_DIR = process.env.STENCIL_SINGLEFILE_OUTDIR
  || resolve(root, 'node_modules/.stencil-singlefile');

const dataUri = file =>
  'data:image/svg+xml,' + encodeURIComponent(readFileSync(resolve(root, file), 'utf8'));

// Rewrite the raw HTML before vite:build-html extracts assets out of it: the classic
// pre-paint script goes inline (it must still run before first paint), the icons become
// data: URIs, and the PWA manifest link + the <meta> CSP go (one file is all inline
// script/style, which the served app's policy forbids — see singleFilePatterns.js) — one file has no shell to install, and
// sw.js registration already no-ops when it can't be fetched.
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
    // index.html moved on and a pattern no longer matches — better a failed build than a
    // "single" file that quietly needs siblings. (tests/singleFileBuild.test.js catches
    // this without a build, but only vite sees the real emitted HTML.)
    for (const stale of NO_SIBLINGS) {
      if (out.includes(stale)) this.error(`index.html still references ${stale} after the single-file rewrite`);
    }
    return out;
  },
});

// Four sibling files can't come along, so their loaders are made to fail fast into the
// fallbacks the app already has — rather than firing a doomed request that a file:// page
// reports as a CORS error.
//   • projectsWorker.js — a SharedWorker is addressed by URL, and a blob: URL is unique
//     per tab, so inlining it would silently stop it being *shared*. Throwing drops
//     tabsCoordinator onto its BroadcastChannel path (its route on any browser without
//     SharedWorker).
//   • imageWorker.js — vite would emit the module Worker as a second file. Throwing drops
//     imageTasks onto its inline path (the same imageRaster.js sequence, on the main thread).
//   • wasm/stencilCore.js — the generated wasm core (gitignored, often absent). The app
//     already degrades to the JS reference implementations the wasm build is parity-tested
//     against, so a single file simply always uses them.
//   • config/openInConfig.json — the operator's gitignored "Open in…" config. Vite treats
//     `new URL(file, import.meta.url)` as an asset and, with assetsInlineLimit: Infinity,
//     would bake whoever built the file's local config into a page meant to be handed
//     around. Throwing drops the loader onto OPEN_IN_DEFAULTS instead.
const useFallbackPaths = () => ({
  name: 'stencil-singlefile-fallbacks',
  enforce: 'pre',
  transform(code, id) {
    if (id.endsWith('core/tabsCoordinator.js')) {
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

// Fold the emitted JS chunk and CSS asset back into the HTML. At writeBundle, not
// generateBundle, so Vite has finished its own chunk rewrites before the code is frozen
// into the page. The now-redundant app.js / app.css are left behind in the staging
// directory, which tools/buildHtml.js deletes.
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
