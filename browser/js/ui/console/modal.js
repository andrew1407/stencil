// The console window: the CLI's command line over the open project, in a window that blocks
// nothing — it opens beside whatever is up and never dims the page. The commands are the
// interpreter's (console/shell/); the typewriter reveal is paced by the Console text speed.
import { StencilElement, define, hostTag, wireModalShell } from '../base.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';
import { createInterpreter } from '../../console/shell/interpreter.js';
import { toFrameIfVideo } from '../../core/image/sourceLoader.js';
import { openImageHere } from '../../core/launch/openFlow.js';
import { consoleModalInner } from './markup.js';
import { createConsoleView } from './view.js';
import { wireConsoleInput } from './input.js';

export class StencilConsoleModal extends StencilElement {
  static inner() { return consoleModalInner(); }

  static template() {
    return hostTag('stencil-console-modal', 'id="console-overlay" class="app-modal-overlay console-overlay"', this.inner());
  }

  wire(app) {
    const overlay = this;
    const input = this.$('console-input');
    const upload = this.$('console-upload');
    const view = createConsoleView(this.$('console-out'));
    let shell;
    const interpreter = createInterpreter({
      app,
      host: {
        print: (lines, kind) => view.print(lines, kind),
        clear: () => view.clear(),
        close: () => shell.close(),
        pickFile: () => upload.click(),
        cliMode: () => false,
        secret: () => false,
      },
    });
    // A bare /upload: the browser's own chooser; a .stencil opens as a project, a picture here.
    upload.addEventListener('change', async () => {
      const file = upload.files?.[0];
      upload.value = '';
      if (!file) return;
      try {
        if (/\.stencil$/i.test(file.name)) await app.export.openProjectFile(file);
        else openImageHere(app, await toFrameIfVideo(file));
      } catch (err) {
        view.print([`error: ${err?.message ?? err}`], 'error');
      }
    });
    wireConsoleInput(input, interpreter);
    shell = wireModalShell(overlay, null, this.$('console-close'), {
      aside: true,
      focusOnOpen: () => input,
      onOpen: () => { if (!view.lines().length) view.print(["Console — '/command <args>' (the '/' is optional). '/help' lists commands, Tab completes, Up/Down recall."], 'plain'); },
    });
    subscribe(EVENTS.consoleToggle, (e) => shell.toggle(e?.detail?.from ?? null));
  }
}

define('stencil-console-modal', StencilConsoleModal);
