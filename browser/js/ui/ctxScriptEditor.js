// The Stencil Script flyout's editor. The flyout is not a list of .ctx-items: typing,
// running and failing leave the menu open. `host` is the menu's own state.
import { notify } from '../utils.js';
import { runScriptHere } from '../console/scriptRunner.js';
import { paintInto, showDiagnostic } from './scriptHighlight.js';

const $ = (id) => document.getElementById(id);

export const wireCtxScriptEditor = (app, host, openScriptWindow) => {
  const item = $('ctx-script');
  const flyout = $('ctx-script-sub');
  const editor = $('ctx-script-editor');
  const pre = $('ctx-script-highlight');
  const strip = $('ctx-script-diag');
  const runBtn = $('ctx-script-run');
  if (!item || !flyout || !editor || !pre || !strip || !runBtn) return;

  let checked = false;   // nothing is reported until the script has been run once
  let running = false;

  // Run, Copy and Download need something to act on; Upload always does.
  const gateActions = () => {
    const empty = editor.value.trim().length === 0;
    for (const id of ['ctx-script-run', 'ctx-script-copy', 'ctx-script-download']) {
      const btn = $(id);
      if (btn) btn.disabled = empty;
    }
  };

  const repaint = () => {
    const program = paintInto(pre, editor.value, checked);
    showDiagnostic(strip, checked ? program : null);
    gateActions();
  };
  // Synchronous, like the window's: the visible text IS the highlight layer, so a deferred
  // paint would read as the characters appearing late.
  const schedule = () => {
    checked = false;   // editing clears the last verdict: it is about older text
    repaint();
  };

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

  // The menu stays open on both outcomes: a failed run is exactly when you want the text
  // and the underlines still in front of you.
  const run = async () => {
    if (running || editor.value.trim().length === 0) return;
    checked = true;   // from here the strip and the underlines mean this exact text
    running = true;
    host.setSending(true);
    try {
      await runScriptHere(editor.value, { app });
    } catch { /* runScript already reported it, and the strip now shows where */ }
    running = false;
    host.setSending(false);
    // The script's last relayout can still be settling.
    host.bumpBusy();
    repaint();
  };

  editor.addEventListener('input', schedule);
  editor.addEventListener('scroll', () => {
    pre.scrollTop = editor.scrollTop;
    pre.scrollLeft = editor.scrollLeft;
  });
  editor.addEventListener('keydown', (e) => {
    if (e.key === 'Tab') { // indent instead of leaving the editor
      e.preventDefault();
      const { selectionStart: a, selectionEnd: b, value } = editor;
      editor.value = `${value.slice(0, a)}  ${value.slice(b)}`;
      editor.selectionStart = a + 2;
      editor.selectionEnd = a + 2;
      schedule();
      return;
    }
    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); run(); }
  });

  runBtn.addEventListener('click', run);
  $('ctx-script-upload-btn').addEventListener('click', () => $('ctx-script-upload').click());
  // Straight into this editor, not through the window's loader: the flyout is where you
  // asked for the file.
  $('ctx-script-upload').addEventListener('change', async (e) => {
    const file = e.target.files?.[0];
    e.target.value = '';
    if (!file) return;
    try {
      editor.value = await file.text();
      schedule();
      notify(`Loaded ${file.name} into the script flyout`, 'info');
    } catch (err) {
      notify(`Could not read ${file.name} — ${err.message || err}`, 'fail');
    }
  });
  $('ctx-script-copy').addEventListener('click', () => {
    navigator.clipboard.writeText(editor.value)
      .then(() => notify('Script copied', 'ok'))
      .catch((err) => notify(`Copy failed: ${err.message || err}`, 'fail'));
  });
  $('ctx-script-download').addEventListener('click', () => {
    const blob = new Blob([editor.value], { type: 'text/plain' });
    app.export.downloadBlob(blob, 'stencil.stc');
  });
  repaint();
};
