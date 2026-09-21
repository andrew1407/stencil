// ── window.stencil.EasterEggs — the logo shows, one call each ─────────────────
// The same entry the header mark's hold and a typed word take, so a call runs the show the
// accent would not: the table is config/logoStage.json.
import { SHOW_NAMES } from '../../ui/logo/stageRules.js';
import { activateShow } from '../../ui/logo/stageTrigger.js';
import { closeLogoStage, markOrigin } from '../../ui/logo/stage.js';

export const createEasterEggsApi = ({ app, guard }) => {
  let stencil;
  const shows = {};
  for (const name of SHOW_NAMES) {
    shows[name] = () => { activateShow(name, app, markOrigin()); return stencil; };
  }
  shows.close = () => { closeLogoStage(); return stencil; };
  // The words themselves, and one call to run any of them: the same list a keyboard spells out.
  shows.what = () => [...SHOW_NAMES];
  shows.of = (word) => {
    const name = SHOW_NAMES.find((n) => n.toLowerCase() === String(word ?? '').trim().toLowerCase());
    if (name) activateShow(name, app, markOrigin());
    return stencil;
  };

  return {
    api: { get EasterEggs() { return guard(shows); } },
    setFacade: (f) => { stencil = f; },
  };
};
