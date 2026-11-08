// The phone clip under usecases/docs/browser/img: the editor on a narrow touch screen, where the
// toolbar folds behind Controls, the picture takes the width under it and the points table sits
// below with a fold of its own. Touch and a coarse pointer are emulated, so the app takes its touch paths.
import { recordGif } from '../lib/shot/shots.mjs';
import { settle, waitForAnimations } from '../lib/waits.mjs';

export function makePhoneSteps({ config, runner, browser, pages }) {
  const clips = config.get('clips');
  // A phone's screen: touch input, and the media a soft-keyboard screen answers (hover: none, pointer: coarse).
  const phone = async (page) => {
    const cdp = await page.context().newCDPSession(page);
    await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 1 });
    await cdp.send('Emulation.setEmulatedMedia', { features: [{ name: 'hover', value: 'none' }, { name: 'pointer', value: 'coarse' }] });
  };
  // One finger down and up: the tap a phone sends, through CDP as e2e's finger does; `stepMs` paces the clip.
  const tapper = async (page, look) => {
    const f = await pages.finger(page);
    return async (x, y) => { await f.down(x, y); await f.up(x, y); await settle(look.stepMs); };
  };
  const centre = async (page, selector) => {
    const b = await page.locator(selector).boundingBox();
    return [b.x + b.width / 2, b.y + b.height / 2];
  };
  const clip = (name, drive) => ({
    name,
    run: () => recordGif(browser, runner.out, name,
      (page, mark) => drive(page, mark, clips[name]), { ...config.gifLook, ...clips[name] }),
  });

  return Object.freeze([
    // A picture with its points open under the folded toolbar: the toolbar shown and folded again, the page
    // scrolled down to the points table, the table folded and brought back.
    clip('phone-layout', async (page, mark, look) => {
      await page.emulateMedia({ colorScheme: runner.themeOf('phone-layout') });
      await phone(page);
      await pages.gotoApp(page);
      await pages.loadTrimmed(page);
      await page.evaluate((lines) => window.stencil.setLines(lines, { history: false }), config.get('pictureLines'));
      // The arrival scrolled the frame into view: back to the top, where Controls is, before the fold.
      await page.evaluate(() => window.scrollTo(0, 0));
      const tap = await tapper(page, look);
      await tap(...await centre(page, '#toggle-controls'));
      await page.locator('#controls-body.hidden').waitFor();
      await waitForAnimations(page);
      await page.evaluate(() => {
        document.querySelectorAll('.notify-toast').forEach((t) => t.remove());
        window.scrollTo(0, 0);
        window.__docsPointer?.hide();
      });
      await settle(look.leadMs);
      await mark();
      await tap(...await centre(page, '#toggle-controls'));
      await page.locator('#controls-body:not(.hidden)').waitFor();
      await settle(look.foldMs);
      await tap(...await centre(page, '#toggle-controls'));
      await page.locator('#controls-body.hidden').waitFor();
      await settle(look.foldMs);
      await page.evaluate(() => document.getElementById('coord-panel').scrollIntoView({ block: 'end', behavior: 'smooth' }));
      await settle(look.scrollMs);
      await tap(...await centre(page, '#toggle-coord-panel'));
      await page.locator('#coord-panel.coord-collapsed').waitFor();
      await settle(look.foldMs);
      await tap(...await centre(page, '#toggle-coord-panel'));
      await settle(look.holdMs);
    }),
  ]);
}
