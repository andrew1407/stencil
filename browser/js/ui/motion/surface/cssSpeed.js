// The stylesheets' half of the Visuals "Animation speed": every finite CSS transition and keyframe
// animation plays at 1 / motionScale(), the rate the JS timers beside them (motionMs) are stretched
// to. Nothing is installed at the default, so the tuned motion is untouched there.
import { motionScale, MOTION_EVENT } from '../motionPrefs.js';
import { subscribe } from '../../../eventBus/appBus.js';

// The theme wipe's view transition is timed by its own --swap-ms (themeSwapPlay.js), already stretched.
const isCss = (a) => ('transitionProperty' in a || 'animationName' in a)
  && !String(a.effect?.pseudoElement ?? '').startsWith('::view-transition');

// Each CSS animation object is rated once, at birth: a new one is made for every restart.
export const rateCssAnimations = (doc, rate, seen = new WeakSet()) => {
  for (const a of doc.getAnimations?.() ?? []) {
    if (seen.has(a) || !isCss(a)) continue;
    seen.add(a);
    if (Number.isFinite(a.effect?.getTiming?.().iterations ?? 1)) a.playbackRate = rate;
  }
};

// A class or a node change may start an animation still in its delay, which no event announces;
// the observer rates it before the delay runs, `transitionrun` / `animationstart` catch the rest
// (an inline-style transition included). getAnimations() forces a style pass, so one per frame.
export const installCssSpeed = (doc = globalThis.document) => {
  if (!doc?.addEventListener || typeof doc.getAnimations !== 'function') return () => {};
  const frame = globalThis.requestAnimationFrame?.bind(globalThis) ?? ((fn) => setTimeout(fn, 16));
  const unframe = globalThis.cancelAnimationFrame?.bind(globalThis) ?? clearTimeout;
  let off = null;
  const sync = () => {
    off?.();
    off = null;
    const scale = motionScale();
    if (scale === 1) return;
    const seen = new WeakSet();
    let pending = null;
    const rate = () => {
      if (pending !== null) return;
      pending = frame(() => { pending = null; rateCssAnimations(doc, 1 / scale, seen); });
    };
    const events = ['transitionrun', 'animationstart'];
    for (const t of events) doc.addEventListener(t, rate, true);
    const watch = typeof MutationObserver === 'function' ? new MutationObserver(rate) : null;
    watch?.observe(doc.documentElement, { subtree: true, childList: true, attributes: true,
      attributeFilter: ['class', 'open', 'hidden'] });
    off = () => {
      for (const t of events) doc.removeEventListener(t, rate, true);
      watch?.disconnect();
      if (pending !== null) unframe(pending);
      pending = null;
    };
  };
  sync();
  const unsubscribe = subscribe(MOTION_EVENT, sync);
  return () => { off?.(); unsubscribe?.(); };
};
