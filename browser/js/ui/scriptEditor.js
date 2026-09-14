// The .stc editor behaviour the script window (scriptModal.js) and the context menu's flyout
// (ctxScriptEditor.js) share: action gating, the paint, Tab-indent, Ctrl+Enter and the four
// actions. Each surface keeps only its own chrome — closing the window, marking the menu busy.
import { notify } from '../utils.js';
import { paintInto, showDiagnostic } from './scriptHighlight.js';

const $ = (id) => document.getElementById(id);

export const wireScriptEditor = ({ editor, pre, strip, ids, app, onRun, onUpload, busy = () => false }) => {
  let checked = false;   // nothing is reported until the script has been run once

  // Run, Copy and Download need something to act on; Upload always does.
  const gateActions = () => {
    const empty = editor.value.trim().length === 0;
    for (const id of [ids.run, ids.copy, ids.download]) {
      const btn = $(id);
      if (btn) btn.disabled = empty;
    }
  };

  const repaint = () => {
    const program = paintInto(pre, editor.value, checked);
    showDiagnostic(strip, checked ? program : null);
    gateActions();
  };
  // Synchronous: the visible text IS the highlight layer, so a deferred paint would read
  // as the characters appearing late.
  const schedule = () => {
    checked = false;   // editing clears the last verdict: it is about older text
    repaint();
  };

  const run = async () => {
    if (busy() || editor.value.trim().length === 0) return;
    checked = true;   // from here the strip and the underlines mean this exact text
    await onRun(editor.value);
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
  $(ids.copy)?.addEventListener('click', () => {
    navigator.clipboard.writeText(editor.value)
      .then(() => notify('Script copied', 'ok'))
      .catch((err) => notify(`Copy failed: ${err.message || err}`, 'fail'));
  });
  $(ids.download)?.addEventListener('click', () => {
    const blob = new Blob([editor.value], { type: 'text/plain' });
    app.export.downloadBlob(blob, 'stencil.stc');
  });

  return { repaint, schedule };
};
