// The script window: write a .stc, see it coloured and checked as you type, run it on the
// open project. The editor's own behaviour is shared with the context menu's flyout
// (scriptEditor.js); only the window's chrome lives here.
import { StencilElement, define, hostTag, wireModalShell } from '../base.js';
import { runScriptHere } from '../../console/scriptRunner.js';
import { notify } from '../../utils.js';
import { wireScriptEditor } from './scriptEditor.js';
import { scriptModalInner } from './scriptModalMarkup.js';

const $ = (id) => document.getElementById(id);

const EDITOR_IDS = Object.freeze({
  run: 'script-run', copy: 'script-copy', download: 'script-download',
  clear: 'script-clear', uploadBtn: 'script-upload-btn', upload: 'script-upload',
});

// One entry point for a dropped or uploaded .stc: into the editor when the window is open,
// straight onto the project when it is not.
export const loadScriptFile = async (file) => {
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
    await runScriptHere(text);
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
    const overlay = $('script-overlay');
    let shell;

    wireScriptEditor({
      editor,
      pre: $('script-highlight'),
      strip: $('script-diag'),
      ids: EDITOR_IDS,
      app,
      // Run leaves the window open — the script applies underneath it — so a second run
      // needs no reopening and a failure keeps its diagnostics in front of you.
      onRun: async (text) => {
        try {
          await runScriptHere(text);
        } catch { /* runScript already reported it, and the strip now shows where */ }
      },
      onUpload: (file) => loadScriptFile(file),
    });

    // The text is the page's ONE script buffer (scriptBuffer.js), shared with the menu flyout, so
    // closing either view throws no work away. Nothing persists it — Clear is the way out.
    shell = wireModalShell(overlay, $('script-btn'), $('script-close'), {
      onOpen: () => {
        // A dropped .stc lands in the editor while the window owns the drop.
        overlay.setAttribute('data-drop-owner', 'script');
        setTimeout(() => editor.focus(), 0);
      },
      onClose: () => { overlay.removeAttribute('data-drop-owner'); },
    });

    overlay.addEventListener('drop', (e) => {
      const file = e.dataTransfer?.files?.[0];
      if (!file?.name?.endsWith('.stc')) return;
      e.preventDefault();
      e.stopPropagation();
      loadScriptFile(file);
    });
  }
}

define('stencil-script-modal', StencilScriptModal);
