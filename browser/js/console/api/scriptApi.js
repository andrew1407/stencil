// window.stencil's .stc entry: run a script string against the live editor.
import { parseScript } from '../../core/script.js';
import { formatDiagnostic } from '../../core/script/diagnostics.js';
import { runScript } from '../scriptRunner.js';

export const createScriptApi = () => {
  let stencil; // the frozen facade, handed over by setFacade after the guard

  const api = {
    /* Run a .stc script against the open project. Resolves to the facade so a script call
     * chains like every other verb; rejects with a ScriptError carrying the line and
     * column when the script does not parse or an op fails.
     *   await stencil.execScript('@crop 10%; @filter bw') */
    execScript(text) {
      return runScript(String(text ?? ''), stencil).then(() => stencil);
    },

    // Parse without running: the diagnostics an editor would underline, already formatted.
    checkScript(text, file = '<script>') {
      const program = parseScript(String(text ?? ''));
      return program.diagnostics.map((d) => formatDiagnostic(file, d));
    },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};
