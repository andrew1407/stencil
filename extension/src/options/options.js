// ── Options page ────────────────────────────────────────────────────────────
// Each section wires itself up as its own module; this file owns the boot order and
// the page-wide chrome (tooltips, themed selects, scroll position).
import './scrollTop.js';
import { initTooltips } from '../lib/controlTooltip.js';
import { enhanceSelect } from '../lib/customSelect.js';
import { motionModeIcon } from '../lib/motionIcons.js';
import { pinToWidestOption } from '../lib/fitWidest.js';
import { wireScrollbarHover } from '../lib/scrollbarHover.js';
import { pinSearchModeEl } from './pinsDom.js';
import './appearance.js';
import './general.js';
import './llm.js';
import './pins.js';
import './connections.js';

// Instant, structured tooltips everywhere on this page (the native `title` waits ~1s
// and never shows on a disabled control). lib/tipContent.js gives them their shape.
initTooltips();
wireScrollbarHover();   // every scrollable's thumb takes the accent under the pointer

// Every <select> on this page gets our own list: the native one is drawn by the OS, in
// system type, ignoring this panel's theme (see lib/customSelect.js). The page-size list
// is long enough to want its filter input.
for (const el of document.querySelectorAll('select'))
  enhanceSelect(el, { search: el.id === 'page', icons: el.id === 'motion' ? motionModeIcon : null });

// The search-mode list is a fixed three-label set, and swapping its label must not
// resize the control and shove the rest of the wrapping filter row — so it is pinned to
// its own widest label (lib/fitWidest.js). The site/storage lists beside it carry
// arbitrary hostnames and size to whatever they show.
pinToWidestOption(pinSearchModeEl);
