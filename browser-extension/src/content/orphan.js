// The ISOLATED content scripts' lifecycle across an extension reload, loaded before each of them:
// the old copy keeps running in an open tab with `chrome.runtime.id` gone. A new copy's ORPHAN
// word, or the orphan's own next event, stands it down: its listeners go and its guard flag is
// freed, so the new copy binds even in the same isolated world. Classic script — no import.
(() => {
  if (window.__stencilOrphan) return;
  const ORPHAN = 'stencil-ext-orphan';
  const alive = () => { try { return !!chrome.runtime?.id; } catch { return false; } };

  // Claims window[flag] for one copy of a script: null while a live copy holds it. `on` binds a
  // listener on `target` that runs only while the extension is alive; `onDown` runs at stand-down.
  const claim = (flag, target, onDown = () => {}) => {
    try { target.dispatchEvent(new Event(ORPHAN)); } catch { /* no DOM events */ }
    if (window[flag]) return null;
    window[flag] = true;
    const bound = [];
    const down = () => {
      for (const [type, fn, opts] of bound) target.removeEventListener(type, fn, opts);
      bound.length = 0;
      delete window[flag];
      onDown();
    };
    const on = (type, fn, opts) => {
      const guarded = (e) => (alive() ? fn(e) : down());
      target.addEventListener(type, guarded, opts);
      bound.push([type, guarded, opts]);
    };
    on(ORPHAN, () => {});   // a live copy ignores the word, whoever dispatched it
    return { on };
  };
  Object.defineProperty(window, '__stencilOrphan', { value: claim, enumerable: false });
})();
