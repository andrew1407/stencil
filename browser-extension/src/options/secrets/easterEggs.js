// window.stencil.EasterEggs on the options page: the logo shows, one call each, taking the same
// entry as the mark's hold and a typed word, plus a `<name>Mode` switch per show.
// Browser twin: js/console/api/easterEggsApi.js.
import { closeLogoStage, markOrigin } from '../../lib/logo/stage.js';
import { PAGE_SHOWS, activateShow, showActive, setShow } from './trigger.js';

// Each call answers `self()` — the whole facade, as the browser's does — so calls chain.
export const createEasterEggs = (doc = document, self = null) => {
  const shows = {};
  const me = () => (self ? self() : shows);
  const run = (name) => activateShow(name, markOrigin(doc), { replace: true });
  for (const name of PAGE_SHOWS) shows[name] = () => { run(name); return me(); };
  shows.close = () => { closeLogoStage(); return me(); };
  // The words themselves, and one call to run any of them: the same list a keyboard spells out.
  shows.what = () => [...PAGE_SHOWS];
  shows.of = (word) => {
    const name = PAGE_SHOWS.find((n) => n.toLowerCase() === String(word ?? '').trim().toLowerCase());
    if (name) run(name);
    return me();
  };
  // Every show as a switch (`neonOnMode = true`); webcoreMode only dresses the page.
  for (const name of PAGE_SHOWS) {
    Object.defineProperty(shows, `${name}Mode`, {
      enumerable: true,
      get: () => showActive(name),
      set: (on) => setShow(name, on, markOrigin(doc)),
    });
  }
  return Object.freeze(shows);
};

// The page's own console facade: only EasterEggs, and nothing the page API exposes elsewhere.
export const installStencilFacade = (win = globalThis, doc = document) => {
  const facade = Object.freeze({ EasterEggs: createEasterEggs(doc, () => facade) });
  Object.defineProperty(win, 'stencil', { value: facade, enumerable: false, configurable: false, writable: false });
  return facade;
};
