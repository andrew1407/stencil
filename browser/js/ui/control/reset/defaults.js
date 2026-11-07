// The defaults a double-click (dblReset.js, beside it) or a logo drop (drag/logoTargets.js) returns a control to:
// the DEFAULTS table by control id, read when the reset lands so a window rendered later still finds its
// own, and the menu rows that mirror a toolbar check (MIRRORS). Desktop twin: support/control/dblReset.hpp.
import { createEditorState } from '../../../core/editorState.js';
import { DEFAULT_PERIOD } from '../../../core/project/meta/projectPeriods.js';
import { defaultSettings as llmDefaults } from '../../../llm/settings.js';
import { DEFAULT_CONSOLE_SPEED, DEFAULT_DRAWING_ANIMATIONS, DEFAULT_MOTION_MODE, DEFAULT_MOTION_SPEED,
  DEFAULT_MULTI_WINDOW } from '../../motion/motionPrefs.js';
import { DEFAULT_SORT_MODE } from '../../projects/window/projectSort.js';
import { SILENCE_MS_DEFAULT } from '../../../llm/voice/settings.js';
import { DEFAULT_NOTIFY_CHANNEL } from '../../../core/settings/notifyChannel.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

// The controls whose default the markup cannot say: read when the double-click lands, so a
// window rendered later still finds its own.
const editor = (key) => () => createEditorState()[key];
// A line's own colours reset to the ones a new line would take: the toolbar pickers'. The toolbar's
// point colour itself takes the toolbar's line colour; an area's fill the Visuals row's.
const fieldOr = (id, key) => (el) => el.ownerDocument?.getElementById(id)?.value || createEditorState()[key];
const toolbarColor = fieldOr('line-color', 'color');
export const DEFAULTS = Object.freeze({
  'page-size': editor('pageSize'), 'unit-select': editor('unit'), 'image-filter': editor('imageFilter'),
  'line-style': editor('style'), 'compare-mode': editor('compareMode'), 'show-points': editor('showPoints'),
  'show-lines': editor('showLines'), 'allow-formulas': editor('allowFormulas'),
  'vs-appearance': () => 'system', 'vs-style': editor('style'), 'vs-motion-mode': () => DEFAULT_MOTION_MODE,
  'vs-draw-anim': () => DEFAULT_DRAWING_ANIMATIONS,
  'vs-notify-channel': () => DEFAULT_NOTIFY_CHANNEL, 'vs-multi-window': () => DEFAULT_MULTI_WINDOW,
  'vs-motion-speed-value': () => DEFAULT_MOTION_SPEED.toFixed(2), 'vs-motion-speed': () => DEFAULT_MOTION_SPEED,
  'vs-console-speed-value': () => DEFAULT_CONSOLE_SPEED.toFixed(2), 'vs-console-speed': () => DEFAULT_CONSOLE_SPEED,
  'vs-thickness': editor('thickness'), 'vs-point': editor('pointSize'),
  'vs-hold-delay': () => constants.HOLD_DRAW.delayMs, 'vs-voice-silence': () => SILENCE_MS_DEFAULT,
  'chat-provider': () => llmDefaults().provider, 'chat-save-chats': () => llmDefaults().saveChats,
  // openImage/modal.js onOpen and cropRows.js state these as literals.
  'open-image-incognito': () => false, 'open-image-rename': () => false, 'open-image-keep': () => true,
  'open-image-crop-toggle': () => false, 'open-image-crop-size': () => 'page',
  'projects-filter': () => 'all', 'projects-sort': () => DEFAULT_SORT_MODE, 'projects-search-mode': () => 'common',
  'connect-autoconnect': () => true, 'connect-sync': () => true, 'connect-filter': () => 'all',
  'expiration-period': () => DEFAULT_PERIOD, 'expiration-auto': () => true, 'expiration-keep': () => false,
  'sel-style': () => 'solid', 'fs-sel-style': () => 'solid', 'open-in-incognito': () => false,
  'ctx-tt-enabled': editor('tooltipEnabled'), 'ctx-tt-page': editor('tooltipShowPage'),
  'ctx-tt-screen': editor('tooltipShowScreen'), 'ctx-tt-coords': editor('tooltipShowCoords'),
  'ctx-allow-formulas': editor('allowFormulas'),
  'line-color': editor('color'), 'sel-color': toolbarColor, 'fs-sel-color': toolbarColor,
  'point-color': toolbarColor, 'sel-point-color': fieldOr('point-color', 'color'),
  'fs-sel-point-color': fieldOr('point-color', 'color'),
  'sel-fill': fieldOr('vs-fill', 'defaultFillColor'), 'fs-sel-fill': fieldOr('vs-fill', 'defaultFillColor'),
  'filter-color': editor('filterColor'), 'ctx-tint-color': editor('filterColor'),
  'vs-line-color': editor('color'), 'vs-fill': editor('defaultFillColor'), 'vs-sel-glow': editor('selGlowColor'),
  'vs-hover-ring': editor('hoverRingColor'), 'vs-focus-ring': editor('focusRingColor'),
  'blank-image-color': () => '#ffffff',
  // Reached by a logo drop only: a double-click in a field selects its text.
  'line-thickness': editor('thickness'), 'point-size': editor('pointSize'),
  'ctx-thickness': editor('thickness'), 'ctx-point-size': editor('pointSize'),
  'formula-x': editor('formulaX'), 'formula-y': editor('formulaY'),
  'ctx-formula-x': editor('formulaX'), 'ctx-formula-y': editor('formulaY'),
});
// Menu rows that toggle in place: their state is the toolbar check they mirror.
export const MIRRORS = Object.freeze({ 'ctx-show-points': 'show-points', 'ctx-show-lines': 'show-lines' });
