// Boot order and the page-wide chrome; each section wires itself up as its own module.
import './scrollTop.js';
import { initTooltips } from '../lib/tip/controlTooltip.js';
import { enhanceSelect } from '../lib/control/customSelect.js';
import { installDblReset } from '../lib/control/dblReset.js';
import { motionModeIcon } from '../lib/motionIcons.js';
import { pinToWidestOption } from '../lib/highlight/fitWidest.js';
import { wireScrollbarHover } from '../lib/control/scrollbarHover.js';
import { pinSearchModeEl } from './pinsDom.js';
import './appearance.js';
import './general.js';
import './llm.js';
import './pins.js';
import './connections.js';

installDblReset(document);

// The native `title` waits ~1 s and never shows on a disabled control.
initTooltips();
wireScrollbarHover();   // every scrollable's thumb takes the accent under the pointer

// The native <select> is drawn by the OS in system type, ignoring the theme (lib/customSelect.js).
for (const el of document.querySelectorAll('select'))
  enhanceSelect(el, { search: el.id === 'page', icons: el.id === 'motion' ? motionModeIcon : null });

// Pinned to its widest label (lib/fitWidest.js) so swapping labels never resizes the wrapping
// filter row; the site/storage lists beside it carry arbitrary hostnames.
pinToWidestOption(pinSearchModeEl);
