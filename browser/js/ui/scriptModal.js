// The script window: write a .stc, see it coloured and checked as you type, run it on the
// open project. The colouring comes from the core's own token stream, so the editor and the
// runner never disagree about what a line means.
import { StencilElement, define, hostTag, wireModalShell } from './base.js';
import { parseScript } from '../core/script.js';
import { runScriptHere } from '../console/scriptRunner.js';
import { notify } from '../utils.js';
import { scriptModalInner } from './scriptModalMarkup.js';


const $ = (id) => document.getElementById(id);

/* The editor and the highlight layer share every metric, so a token's span in one lands on
 * the same pixel in the other; only the classes differ. Diagnostics are painted only once
 * the script has been RUN: a half-typed line is not a mistake, and underlining it as one
 * while you are still writing reads as nagging. */
const paintInto = (pre, text, withDiagnostics) => {
  const program = parseScript(text);
  while (pre.firstChild) pre.removeChild(pre.firstChild);

  const marks = program.tokens.map((t) => ({ line: t.line, col: t.col, len: t.len, cls: `stk-${t.kind}` }));
  if (withDiagnostics) {
    for (const d of program.diagnostics) {
      const len = Math.max(1, d.len);
      // A diagnostic underlines the token already there rather than replacing it, so the
      // span keeps its colour AND gains the squiggle.
      const over = marks.filter((m) => m.line === d.line
        && m.col < d.col + len && d.col < m.col + Math.max(1, m.len));
      if (over.length > 0) for (const m of over) m.cls += ` stk-${d.severity}`;
      else marks.push({ line: d.line, col: d.col, len, cls: `stk-${d.severity}` });
    }
  }

  const lines = text.split('\n');
  lines.forEach((lineText, i) => {
    const spans = marks.filter((m) => m.line === i + 1).sort((a, b) => a.col - b.col);
    let at = 0;
    for (const m of spans) {
      const start = m.col - 1;
      if (start < at || start > lineText.length) continue;
      if (start > at) pre.appendChild(document.createTextNode(lineText.slice(at, start)));
      const span = document.createElement('span');
      span.className = m.cls;
      span.textContent = lineText.slice(start, start + m.len);
      pre.appendChild(span);
      at = start + m.len;
    }
    if (at < lineText.length) pre.appendChild(document.createTextNode(lineText.slice(at)));
    if (i < lines.length - 1) pre.appendChild(document.createTextNode('\n'));
  });
  return program;
};

const showDiagnostic = (strip, program) => {
  const first = program?.diagnostics.find((d) => d.severity === 'error')
    ?? program?.diagnostics.find((d) => d.severity === 'warning');
  strip.textContent = first ? `Line ${first.line}:${first.col} — ${first.message}` : '';
  strip.className = `script-diag${first ? ` script-diag-${first.severity}` : ''}`;
};

// One entry point for a dropped or uploaded .stc: into the editor when the window is open,
// straight onto the project when it is not.
export const loadScriptFile = async (file, { app } = {}) => {
  const text = await file.text();
  const editor = $('script-editor');
  const overlay = $('script-overlay');
  if (editor && overlay?.classList?.contains('modal-open')) {
    editor.value = text;
    editor.dispatchEvent(new Event('input'));
    notify(`Loaded ${file.name} into the script window`, 'info');
    return;
  }
  try {
    await runScriptHere(text, app ? { app } : undefined);
  } catch { /* runScript already reported it */ }
};

export const isScriptModalOpen = () => !!$('script-overlay')?.classList?.contains('modal-open');

export class StencilScriptModal extends StencilElement {
  static inner() {
    return scriptModalInner();
  }

  static template() {
    return hostTag('stencil-script-modal', 'id="script-overlay" class="app-modal-overlay"', this.inner());
  }

  wire(app) {
    const editor = $('script-editor');
    const pre = $('script-highlight');
    const strip = $('script-diag');
    const overlay = $('script-overlay');
    let checked = false;   // nothing is reported until the script has been run once

    // Run, Copy and Download need something to act on; Upload always does.
    const gateActions = () => {
      const empty = editor.value.trim().length === 0;
      $('script-run').disabled = empty;
      $('script-copy').disabled = empty;
      $('script-download').disabled = empty;
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

    // The window opens EMPTY every time — nothing is kept between opens or across a reload.
    // A script you meant to run is one you just wrote; a stale one sitting there is a trap.
    const shell = wireModalShell(overlay, $('script-btn'), $('script-close'), {
      onOpen: () => {
        editor.value = '';
        checked = false;
        repaint();
        // A dropped .stc lands in the editor while the window owns the drop.
        overlay.setAttribute('data-drop-owner', 'script');
        setTimeout(() => editor.focus(), 0);
      },
      onClose: () => {
        overlay.removeAttribute('data-drop-owner');
        editor.value = '';
        checked = false;
        repaint();
      },
    });

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

    const run = async () => {
      if (editor.value.trim().length === 0) return;
      checked = true;   // from here the strip and the underlines mean this exact text
      try {
        await runScriptHere(editor.value, { app });
        shell.close();
      } catch { /* runScript already reported it, and the strip now shows where */ }
      repaint();
    };

    $('script-run').addEventListener('click', run);
    $('script-upload-btn').addEventListener('click', () => $('script-upload').click());
    $('script-upload').addEventListener('change', (e) => {
      const file = e.target.files?.[0];
      if (file) loadScriptFile(file, { app });
      e.target.value = '';
    });
    $('script-copy').addEventListener('click', () => {
      navigator.clipboard.writeText(editor.value)
        .then(() => notify('Script copied', 'ok'))
        .catch((err) => notify(`Copy failed: ${err.message || err}`, 'fail'));
    });
    $('script-download').addEventListener('click', () => {
      const blob = new Blob([editor.value], { type: 'text/plain' });
      app.export.downloadBlob(blob, 'stencil.stc');
    });
    overlay.addEventListener('drop', (e) => {
      const file = e.dataTransfer?.files?.[0];
      if (!file?.name?.endsWith('.stc')) return;
      e.preventDefault();
      e.stopPropagation();
      loadScriptFile(file, { app });
    });
  }
}

define('stencil-script-modal', StencilScriptModal);
