// ── window.stencil.EasterEggs — the logo shows, one call each ─────────────────
// The same entry the header mark's hold and a typed word take, so a call runs the show the
// accent would not: the table is config/logoStage.json.
import { SHOW_NAMES } from '../../ui/logo/stageRules.js';
import { activateShow, showActive, setShow } from '../../ui/logo/stageTrigger.js';
import { closeLogoStage, markOrigin } from '../../ui/logo/stage.js';
import { enterCliMode, exitCliMode } from '../../ui/cli/mode.js';
import { cliModeOn, cliModeGate } from '../shell/gate.js';
import { CLI_MODE } from '../shell/table.js';

const CLI_WORDS = Object.freeze([CLI_MODE.typedWord, 'cli']);

export const createEasterEggsApi = ({ app, guard }) => {
  let stencil;
  const shows = {};
  const run = (name) => { cliModeGate('a logo show'); return activateShow(name, app, markOrigin(), { replace: true }); };
  for (const name of SHOW_NAMES) {
    shows[name] = () => { run(name); return stencil; };
  }
  shows.close = () => { closeLogoStage(); return stencil; };
  // The words themselves, and one call to run any of them: the same list a keyboard spells out,
  // the CLI mode's word among them.
  shows.what = () => [...SHOW_NAMES, ...CLI_WORDS];
  shows.of = (word) => {
    const w = String(word ?? '').trim().toLowerCase();
    if (CLI_WORDS.includes(w)) { enterCliMode(app); return stencil; }
    const name = SHOW_NAMES.find((n) => n.toLowerCase() === w);
    if (name) run(name);
    return stencil;
  };
  // The terminal over the page (ui/cli/mode.js); while it is on, the facade's UI members refuse.
  Object.defineProperty(shows, 'cliMode', {
    enumerable: true,
    get: () => cliModeOn(),
    set: (on) => { if (!!on === cliModeOn()) return; if (on) enterCliMode(app); else exitCliMode(app); },
  });
  // Every show as a switch (`neonOnMode = true`); webcoreMode only dresses the page, never opens one.
  for (const name of SHOW_NAMES) {
    Object.defineProperty(shows, `${name}Mode`, {
      enumerable: true,
      get: () => showActive(name, app),
      set: (on) => { if (on) cliModeGate('a logo show'); setShow(name, app, on); },
    });
  }

  return {
    api: { get EasterEggs() { return guard(shows); } },
    setFacade: (f) => { stencil = f; },
  };
};
