import { isSplitCompare } from '../../../utils.js';
import { setChecked } from '../../control/swap.js';
import { COMPARE_MODES } from '../../../core/settings/controller.js';
import { contextMenuPoint } from './hotkeyRules.js';
// Every hotkey id the editor answers to, as one table of actions — plus the two sets
// the dispatcher consults: which are edits (inert while comparing) and which act on
// the selection. The keydown loop above is the only caller.
export function hotkeyActions(app) {
  const clickIfActive = id => {
    const el = document.getElementById(id);
    if (!el || el.disabled) return;
    // Alt+<letter> presses Alt first, and Alt over an icon opens its window as a hover-peek
    // (ui/popover.js); without claiming the peek the letter would toggle it straight back shut.
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
    // With a line selected Alt+R arms the line-rotate chord (Alt+R+←/→, wireArrowPan), so it must
    // not also spin the image. Alt+Shift+R is unaffected by the chord.
    rotateImageLeft: () => { if (app.image && app.selectedIndices().length === 0) app.imageModel.rotateImage(-1); },
    rotateImageRight: () => { if (app.image) app.imageModel.rotateImage(1); },
    // Alt+Shift+Arrow transforms of the SELECTED line — flip about / rotate ±90 around its bbox
    // centre (same pivot as the arbitrary-angle rotate). The methods no-op without a selection.
    flipLineHorizontal: () => app.flipSelectedLine(true),
    flipLineVertical: () => app.flipSelectedLine(false),
    rotateLineCW90: () => app.rotateSelectedLineQuarter(1),
    rotateLineCCW90: () => app.rotateSelectedLineQuarter(-1),
    // Ctrl+C's slot is 'split' while a split compare view is active, else 'current'. Resolved HERE,
    // not in copyImageToClipboard: the write must run synchronously inside this gesture.
    copyImage: () => app.export.copyImageToClipboard(isSplitCompare(app) ? 'split' : 'current'),
    copyImageOriginal: () => app.export.copyImageToClipboard('original'),
    copyImageTint: () => app.export.copyImageToClipboard('tint'),
    copyLayout: () => app.export.copyLayoutToClipboard(),
    // paste is handled by the native 'paste' event listener below — entry here is for hotkey display only
    paste: () => { /* handled by paste event */ },
    clearAllLines: () => app.clearAllLines(),
    // Deletes the WHOLE selection (selectedIndices() falls back to [selectedLineIdx]); with a
    // POINT focused the chord narrows to that point.
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
    // An open modal owns Escape first: it closes on this very keypress, so also dropping the canvas
    // selection behind it would be an invisible side effect of closing a dialog.
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
    openAssistantSettings: () => clickIfActive('chat-settings-btn'),
    openHelp: () => clickIfActive('info-btn'),
    openHotkeys: () => clickIfActive('settings-btn'),
    openVisuals: () => clickIfActive('visuals-btn'),
    // Shift+F10 raises a REAL contextmenu event on the viewport, so the right-click path
    // (contextMenu.js) stays the only path.
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
