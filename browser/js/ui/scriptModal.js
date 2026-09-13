// The script window: write a .stc, see it coloured and checked as you type, run it on the
// open project. The colouring comes from the core's own token stream, so the editor and the
// runner never disagree about what a line means.
import { StencilElement, define, hostTag, wireModalShell } from './base.js';
import { parseScript } from '../core/script.js';
import { runScriptHere } from '../console/scriptRunner.js';
import { notify } from '../utils.js';
import { scriptModalInner } from './scriptModalMarkup.js';
import constants from '../config/constants.json' with { type: 'json' };

const DRAFT_KEY = constants.STORAGE_KEYS?.script ?? 'drawingApp_script';
const REPAINT_MS = 60;

const $ = (id) => document.getElementById(id);

// The editor and the highlight layer share every metric, so a token's span in one lands on
// the same pixel in the other; only the classes differ.
const paintInto = (pre, text) => {
  const program = parseScript(text);
  while (pre.firstChild) pre.removeChild(pre.firstChild);

  const marks = [];
  for (const t of program.tokens) marks.push({ line: t.line, col: t.col, len: t.len, cls: `stk-${t.kind}` });
  for (const d of program.diagnostics) {
    marks.push({ line: d.line, col: d.col, len: Math.max(1, d.len), cls: `stk-${d.severity}` });
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
  const first = program.diagnostics.find((d) => d.severity === 'error')
    ?? program.diagnostics.find((d) => d.severity === 'warning');
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
    let timer = null;

    const repaint = () => {
      const program = paintInto(pre, editor.value);
      showDiagnostic(strip, program);
    };
    const schedule = () => {
      if (timer) clearTimeout(timer);
      timer = setTimeout(repaint, REPAINT_MS);
    };

    const shell = wireModalShell(overlay, $('script-btn'), $('script-close'), {
      onOpen: () => {
        try { editor.value = localStorage.getItem(DRAFT_KEY) ?? ''; } catch { /* storage blocked */ }
        repaint();
        // A dropped .stc lands in the editor while the window owns the drop.
        overlay.setAttribute('data-drop-owner', 'script');
        setTimeout(() => editor.focus(), 0);
      },
      onClose: () => {
        overlay.removeAttribute('data-drop-owner');
        if (app?.storage?.incognito) return;
        try { localStorage.setItem(DRAFT_KEY, editor.value); } catch { /* storage blocked */ }
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
      try {
        await runScriptHere(editor.value, { app });
        shell.close();
      } catch { /* runScript already reported it, and the strip shows where */ }
      repaint();
    };

    $('script-run').addEventListener('click', run);
    $('script-upload-btn').addEventListener('click', () => $('script-upload').click());
    $('script-upload').addEventListener('change', (e) => {
      const file = e.target.files?.[0];
      if (file) loadScriptFile(file, { app });
      e.target.value = '';
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
