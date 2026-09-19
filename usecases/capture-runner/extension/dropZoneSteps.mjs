// The drag-time shot for usecases/docs/browser-extension/img: the 2x2 overlay the panel paints on
// the page while a scanned row is dragged out of it. It only exists mid-drag, so this arms it,
// shoots, and takes it down again.
import { readFileSync } from 'node:fs';
import { repoPath } from '../lib/paths.mjs';

// The background injects this with chrome.scripting.executeScript({ func: mountDropZones }), which
// serialises that function and runs it in the page; evaluating the same source is that mechanism.
// The overlay then arms itself off a dragover carrying a URL (lib/dropZones.js onOver).
const SRC = readFileSync(repoPath('browser-extension/src/lib/dropZones.js'), 'utf8');

// Fractions of the viewport: quadrantAt splits it in four — here / incognito on top, newtab / crop
// below — so a point picks the quadrant whose label the shot is meant to show highlighted.
const QUADS = Object.freeze({ here: [0.25, 0.3], incognito: [0.75, 0.3], crop: [0.75, 0.72] });

export function makeDropZoneSteps({ config, runner, host, applyShellTheme, timeouts }) {
  const arm = async (page, mode, quad) => {
    await page.evaluate(([src, accent, scheme, at]) => {
      const mount = new Function(`${src.replace(/export const/g, 'const')}; return mountDropZones;`)();
      mount(accent, false, scheme);
      const dt = new DataTransfer();
      dt.setData('text/uri-list', 'https://stencil.example/photo.jpg');
      window.dispatchEvent(new DragEvent('dragover', {
        bubbles: true,
        cancelable: true,
        dataTransfer: dt,
        clientX: Math.round(window.innerWidth * at[0]),
        clientY: Math.round(window.innerHeight * at[1]),
      }));
    }, [SRC, config.get('dropZones.accent'), mode, QUADS[quad]]);
    await page.waitForFunction(() => {
      const el = document.getElementById('stencil-ext-dropzones');
      return !!el && el.style.opacity === '1' && !!el.shadowRoot?.querySelector('.over');
    }, null, { timeout: timeouts.scanMs });
  };

  return Object.freeze([
    {
      name: 'site-dropzones',
      run: async (ctx, theme) => {
        await host.bringToFront();
        await applyShellTheme(host, theme);
        await arm(host, theme, 'here');
        await runner.shot(host, 'site-dropzones');
        await host.evaluate(() => document.getElementById('stencil-ext-dropzones')?.remove());
      },
    },
  ]);
}
