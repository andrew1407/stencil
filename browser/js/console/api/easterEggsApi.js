// ── window.stencil.EasterEggs — the logo shows, one call each ─────────────────
// The same entry the header mark's hold and a typed word take, so a call runs the show the
// accent would not: the table is config/logoStage.json.
import { SHOW_NAMES } from '../../ui/logo/stageRules.js';
import { activateShow, showActive, setShow } from '../../ui/logo/stageTrigger.js';
import { closeLogoStage, markOrigin } from '../../ui/logo/stage.js';

export const createEasterEggsApi = ({ app, guard }) => {
  let stencil;
  const shows = {};
  const run = (name) => activateShow(name, app, markOrigin(), { replace: true });
  for (const name of SHOW_NAMES) {
    shows[name] = () => { run(name); return stencil; };
  }
  shows.close = () => { closeLogoStage(); return stencil; };
  // The words themselves, and one call to run any of them: the same list a keyboard spells out.
  shows.what = () => [...SHOW_NAMES];
  shows.of = (word) => {
    const name = SHOW_NAMES.find((n) => n.toLowerCase() === String(word ?? '').trim().toLowerCase());
    if (name) run(name);
    return stencil;
  };
  // Every show as a switch (`neonOnMode = true`); webcoreMode only dresses the page, never opens one.
  for (const name of SHOW_NAMES) {
    Object.defineProperty(shows, `${name}Mode`, {
      enumerable: true,
      get: () => showActive(name, app),
      set: (on) => setShow(name, app, on),
    });
  }

  return {
    api: { get EasterEggs() { return guard(shows); } },
    setFacade: (f) => { stencil = f; },
  };
};
