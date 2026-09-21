// Shape of scriptApi.js — the facade part that runs a .stc.
import type { Stencil } from '../stencilApi.js';
export const createScriptApi: () => {
  api: {
    execScript(text: string): Promise<Stencil>;
    checkScript(text: string, file?: string): string[];
  };
  setFacade: (facade: Stencil) => void;
};
