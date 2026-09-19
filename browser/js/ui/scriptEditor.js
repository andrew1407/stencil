// The .stc editor behaviour the script window (scriptModal.js) and the context menu's flyout
// (ctxScriptEditor.js) share: action gating, the paint, Tab-indent, Ctrl+Enter and the five
// actions. Each surface keeps only its own chrome — closing the window, marking the menu busy.
// The text itself belongs to neither: both read and write the one buffer (scriptBuffer.js).
import { notify } from '../utils.js';
import { scriptText, setScriptText, subscribeScript } from './scriptBuffer.js';
import { paintInto, showDiagnostic } from './scriptHighlight.js';

const $ = (id) => document.getElementById(id);

export const wireScriptEditor = ({ editor, pre, strip, ids, app, onRun, onUpload, busy = () => false }) => {
  let checked = false;   // nothing is reported until the script has been run once

  // Run needs something to DO — a script of only comments lowers to no ops. An errored script
  // still runs: the strip and the underlines are how the errors become visible.
  const gateActions = (program) => {
    const blank = scriptText().trim().length === 0;
    for (const id of [ids.copy, ids.download, ids.clear]) {
      const btn = $(id);
      if (btn) btn.disabled = blank;
    }
    const idle = !program || (program.ops.length === 0 && program.diagnostics.length === 0);
    const runBtn = $(ids.run);
    if (runBtn) runBtn.disabled = blank || idle;
  };

  const repaint = () => {
    const program = paintInto(pre, scriptText(), checked);
    showDiagnostic(strip, checked ? program : null);
    gateActions(program);
  };
  // Synchronous: the visible text IS the highlight layer, so a deferred paint would read
  // as the characters appearing late.
  const schedule = () => {
    checked = false;   // editing clears the last verdict: it is about older text
    setScriptText(editor.value, adopt);
    repaint();
  };
  // The other view changed the shared text: show it, and drop a verdict about the older one.
  const adopt = (next) => { editor.value = next; checked = false; repaint(); };

  const run = async () => {
    if (busy() || $(ids.run)?.disabled) return;   // Ctrl+Enter obeys the same gate as the button
    checked = true;   // from here the strip and the underlines mean this exact text
    await onRun(scriptText());
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

  $(ids.run)?.addEventListener('click', run);
  $(ids.uploadBtn)?.addEventListener('click', () => $(ids.upload)?.click());
  $(ids.upload)?.addEventListener('change', (e) => {
    const file = e.target.files?.[0];
    e.target.value = '';
    if (file) onUpload(file);
  });
  $(ids.clear)?.addEventListener('click', () => { editor.value = ''; schedule(); });
  $(ids.copy)?.addEventListener('click', () => {
    navigator.clipboard.writeText(scriptText())
      .then(() => notify('Script copied', 'ok'))
      .catch((err) => notify(`Copy failed: ${err.message || err}`, 'fail'));
  });
  $(ids.download)?.addEventListener('click', () => {
    const blob = new Blob([scriptText()], { type: 'text/plain' });
    app.export.downloadBlob(blob, 'stencil.stc');
  });

  const dispose = subscribeScript(adopt);
  adopt(scriptText());   // the buffer, not this textarea, says what is on screen
  return { schedule, dispose };
};
