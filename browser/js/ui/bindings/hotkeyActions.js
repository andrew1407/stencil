import { isSplitCompare } from '../../utils.js';
import { setChecked } from '../controlSwap.js';
import { COMPARE_MODES } from '../../core/settingsController.js';
import { contextMenuPoint } from './hotkeyRules.js';
// Every hotkey id the editor answers to, as one table of actions — plus the two sets
// the dispatcher consults: which are edits (inert while comparing) and which act on
// the selection. The keydown loop above is the only caller.
export function hotkeyActions(app) {
  const clickIfActive = id => {
    const el = document.getElementById(id);
    if (!el || el.disabled) return;
    // Alt+<letter> presses Alt first, and Alt over an icon opens its window as a hover-peek
    // (ui/popover.js). Without this the letter would arrive to find the window already open
    // and toggle it straight back shut. Claiming the peek keeps what the user asked for.
    if (el.__stencilGestures?.hotkey?.()) return;
    el.click();
  };
  // Keyboard shortcuts — dispatched via the hotkeys registry
  const HK_HANDLERS = {
    undo: () => { if (!document.getElementById('undo').disabled) app.undo(); },
    redo: () => { if (!document.getElementById('redo').disabled) app.redo(); },
    startDraw: () => { if (app.image && !app.isDrawing) app.startDrawingMode(); },
    stopDraw: () => { if (app.isDrawing) app.stopDrawingMode(); },
    togglePoints: () => {
      const cb = document.getElementById('show-points');
      setChecked(cb, !cb.checked);
      app.showPoints = cb.checked;
      app.renderer.redraw();
    },
    toggleLines: () => {
      const cb = document.getElementById('show-lines');
      setChecked(cb, !cb.checked);
      app.showLines = cb.checked;
      app.renderer.redraw();
    },
    cycleFilter: () => {
      const opts = ['none', 'bw', 'sepia', 'invert', 'contour', 'custom'];
      const cur = opts.indexOf(app.imageFilter);
      // Route through setImageFilter so the cycle marks the filter dirty + syncs to the server.
      app.settings.setImageFilter(opts[(cur + 1) % opts.length]);
    },
    cycleCompare: () => {
      if (!app.image) return;
      const cur = COMPARE_MODES.indexOf(app.compareMode);
      app.settings.setCompareMode(COMPARE_MODES[(cur + 1) % COMPARE_MODES.length]);
    },
    resetZoom: () => app.zoomPan.fitToWindow(),
    toggleControls: () => { const b = document.getElementById('toggle-controls');   if (b) b.click(); },
    togglePointsList: () => { const b = document.getElementById('toggle-coord-panel'); if (b) b.click(); },
    fullscreen: () => app.toggleFullscreen?.(),
    zoomIn: () => app.zoomPan.zoomAroundCenter(app.scale + 0.25),
    zoomOut: () => app.zoomPan.zoomAroundCenter(app.scale - 0.25),
    zoomInBig: () => app.zoomPan.zoomAroundCenter(app.scale + 1.0),
    zoomOutBig: () => app.zoomPan.zoomAroundCenter(app.scale - 1.0),
    // Alt+R rotates the IMAGE — but with a line selected it instead arms the line-rotate
    // chord (Alt+R+←/→, handled in wireArrowPan), so it must not also spin the image.
    // Deselect to rotate the image again. Alt+Shift+R (right) is unaffected by the chord.
    rotateImageLeft: () => { if (app.image && app.selectedIndices().length === 0) app.imageModel.rotateImage(-1); },
    rotateImageRight: () => { if (app.image) app.imageModel.rotateImage(1); },
    // Alt+Shift+Arrow transforms of the SELECTED line — flip about / rotate ±90 around its bbox
    // centre (same pivot as the arbitrary-angle rotate). The methods no-op without a selection.
    flipLineHorizontal: () => app.flipSelectedLine(true),
    flipLineVertical: () => app.flipSelectedLine(false),
    rotateLineCW90: () => app.rotateSelectedLineQuarter(1),
    rotateLineCCW90: () => app.rotateSelectedLineQuarter(-1),
    // Ctrl+C's OWN slot is 'split' while a split compare view is active — that view is
    // what's actually on screen right now — and 'current' (plain tint+lines) otherwise.
    // Resolved HERE, not inside copyImageToClipboard: the write must run synchronously
    // inside this gesture (exportService.js), so it can't route through the toolbar
    // button's click (openFull resolves the same way, for the mouse-click/saveImage path).
    copyImage: () => app.export.copyImageToClipboard(isSplitCompare(app) ? 'split' : 'current'),
    copyImageOriginal: () => app.export.copyImageToClipboard('original'),
    copyImageTint: () => app.export.copyImageToClipboard('tint'),
    copyLayout: () => app.export.copyLayoutToClipboard(),
    // paste is handled by the native 'paste' event listener below — entry here is for hotkey display only
    paste: () => { /* handled by paste event */ },
    clearAllLines: () => app.clearAllLines(),
    // Deletes the WHOLE selection (selectedIndices() falls back to [selectedLineIdx]);
    // on Mac the default reads as ⌥⌫. With a POINT focused, the chord narrows to that
    // point — same route-a-shared-chord-by-selection pattern as Alt+Shift+Arrow.
    deleteLine: () => {
      if (app.isDrawing) return;
      if (app.coordLineIdx >= 0 && app.focusedPtIdx >= 0) {
        app.removePoint(app.coordLineIdx, app.focusedPtIdx);
        return;
      }
      app.removeSelectedLines();
    },
    // Delete the focused point of the selected line (the point, not the whole line).
    deletePoint: () => {
      if (app.isDrawing) return;
      if (app.coordLineIdx >= 0 && app.focusedPtIdx >= 0) app.removePoint(app.coordLineIdx, app.focusedPtIdx);
    },
    // Esc clears the selection — the desktop's "Deselect" action bound to the same key.
    // An open modal owns Escape first: it closes on this very keypress, so also dropping
    // the canvas selection behind it would be an invisible side effect of closing a dialog.
    deselect: () => { if (!document.querySelector('.modal-open')) app.deselectLine(); },
    // Toolbar/menu openers — each just drives the matching button so the shortcut and the
    // click path stay identical (clickIfActive skips a disabled control, like the UI does).
    loadImage: () => clickIfActive(app.image ? 'open-image-btn' : 'load-image-btn'),
    openAnotherImage: () => clickIfActive(app.image ? 'open-image-btn' : 'load-image-btn'),
    openIn: () => clickIfActive('open-in-btn'),
    saveImage: () => clickIfActive('save-image'),
    saveImageOriginal: () => app.export.saveImage('original'),
    saveImageTint: () => app.export.saveImage('tint'),
    // Only rendered where the Web Share API takes files (mobile/PWA); exportService
    // guards the unsupported case with a toast, so the chord is safe everywhere.
    shareImage: () => clickIfActive('share-image'),
    cropImage: () => clickIfActive('crop-image'),
    downloadJson: () => clickIfActive('download-json'),
    uploadJson: () => clickIfActive('upload-json-btn'),
    openScript: () => clickIfActive('script-btn'),
    saveProject: () => clickIfActive('save-project-btn'),
    openProject: () => clickIfActive('open-project-btn'),
    toggleLiveSync: () => clickIfActive('live-sync-btn'),
    deleteProject: () => clickIfActive('delete-project-btn'),
    // Remove the CURRENT project from the editor (the trash in Data) — distinct from
    // deleteProject above, which deletes the .stencil file on disk.
    clearProject: () => clickIfActive('clear-storage'),
    // The ✎ next to the project name: enters inline rename (focus + select). A no-op
    // with no saved project, exactly as clicking the pencil is.
    renameProject: () => clickIfActive('project-name-edit'),
    openProjects: () => clickIfActive('projects-btn'),
    openServers: () => clickIfActive('connect-btn'),
    openLinks: () => clickIfActive('links-btn'),
    openDescription: () => clickIfActive('description-btn'),
    openKeywords: () => clickIfActive('keywords-btn'),
    toggleTheme: () => clickIfActive('theme-toggle'),
    toggleIncognito: () => clickIfActive('incognito-toggle'),
    toggleChat: () => clickIfActive('chat-btn'),
    toggleVoiceChat: () => clickIfActive('voice-chat-btn'),
    // The assistant's gear lives inside the chat composer's "…" menu; its click is the
    // modal's own opener, so the window flies to/from the "…" (or falls from above when
    // no chat surface is on screen) exactly as the menu route does.
    openAssistantSettings: () => clickIfActive('chat-settings-btn'),
    openHelp: () => clickIfActive('info-btn'),
    openHotkeys: () => clickIfActive('settings-btn'),
    openVisuals: () => clickIfActive('visuals-btn'),
    // Shift+F10 opens the canvas context menu from the keyboard, as a REAL contextmenu
    // event on the viewport so the right-click path (contextMenu.js) is the only path.
    // An already-open menu keeps its place; Escape closes it.
    contextMenu: () => {
      const viewport = document.getElementById('canvas-viewport');
      if (!viewport || document.getElementById('ctx-menu')?.classList.contains('ctx-open')) return;
      const { x, y } = contextMenuPoint(app, viewport.getBoundingClientRect());
      viewport.dispatchEvent(new MouseEvent('contextmenu', { clientX: x, clientY: y, bubbles: true, cancelable: true }));
    }
  };
  // Editing hotkeys are inert while a compare view is active (it's read-only).
  const EDIT_HOTKEYS = new Set([
    'startDraw', 'stopDraw', 'undo', 'redo', 'clearAllLines', 'deleteLine', 'deletePoint',
    'flipLineHorizontal', 'flipLineVertical', 'rotateLineCW90', 'rotateLineCCW90',
  ]);
  // The Alt+Shift+Arrow line transforms — flip ↑/↓, rotate ±90 ↔ — act on the selection only.
  // ↑/↓ share their combo with big-zoom, so the loop routes the shared chord by selection below.
  const LINE_TRANSFORM_HOTKEYS = new Set(['flipLineHorizontal', 'flipLineVertical', 'rotateLineCW90', 'rotateLineCCW90']);
  return { HK_HANDLERS, EDIT_HOTKEYS, LINE_TRANSFORM_HOTKEYS };
}
