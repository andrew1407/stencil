// The Stencil Script flyout's editor. The flyout is not a list of .ctx-items: typing,
// running and failing leave the menu open. `host` is the menu's own state. The editor's
// behaviour is the window's, shared through scriptEditor.js. Returns the editor's drop off
// the shared buffer (scriptBuffer.js).
import { notify } from '../utils.js';
import { runScriptHere } from '../console/scriptRunner.js';
import { wireScriptEditor } from './scriptEditor.js';

const $ = (id) => document.getElementById(id);

const EDITOR_IDS = Object.freeze({
  run: 'ctx-script-run', copy: 'ctx-script-copy', download: 'ctx-script-download',
  clear: 'ctx-script-clear', uploadBtn: 'ctx-script-upload-btn', upload: 'ctx-script-upload',
});

export const wireCtxScriptEditor = (app, host, openScriptWindow) => {
  const item = $('ctx-script');
  const flyout = $('ctx-script-sub');
  const editor = $('ctx-script-editor');
  const pre = $('ctx-script-highlight');
  const strip = $('ctx-script-diag');
  const runBtn = $('ctx-script-run');
  if (!item || !flyout || !editor || !pre || !strip || !runBtn) return;

  let running = false;

  // Engaged = the caret is in the flyout or a script is running: the hover-out timers must
  // not yank the editor away then (keepSubOpen consults this).
  flyout._keepOpen = () => running || flyout.contains(document.activeElement);
  // Anything clicked in here may make the editor relayout: hold the scroll grace open.
  flyout.addEventListener('click', () => { host.bumpBusy(); });
  // Clicking the parent opens the flyout too and drops the caret into the editor.
  item.addEventListener('click', (e) => {
    if (flyout.contains(e.target)) return;
    if (item.dataset.noSub === '1') {
      host.closeMenu();
      openScriptWindow();
      return;
    }
    if (!flyout.classList.contains('ctx-sub-visible')) {
      host.positionSub(item, flyout);
      host.setActiveSub(flyout, item);
    }
    editor.focus();
  });

  const { schedule, dispose } = wireScriptEditor({
    editor,
    pre,
    strip,
    ids: EDITOR_IDS,
    app,
    busy: () => running,
    // The menu stays open on both outcomes: a failed run is exactly when you want the text
    // and the underlines still in front of you.
    onRun: async (text) => {
      running = true;
      host.setSending(true);
      try {
        await runScriptHere(text);
      } catch { /* runScript already reported it, and the strip now shows where */ }
      running = false;
      host.setSending(false);
      host.bumpBusy();   // the script's last relayout can still be settling
    },
    // Straight into this editor, not through the window's loader: the flyout is where you
    // asked for the file.
    onUpload: async (file) => {
      try {
        editor.value = await file.text();
        schedule();
        notify(`Loaded ${file.name} into the script flyout`, 'info');
      } catch (err) {
        notify(`Could not read ${file.name} — ${err.message || err}`, 'fail');
      }
    },
  });
  return dispose;
};
