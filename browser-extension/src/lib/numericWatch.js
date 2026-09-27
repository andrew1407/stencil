// Every numeric field on an extension page takes an expression, the ones a panel or a menu
// renders later included: one page-wide observer upgrades them as they appear. The browser app
// does the same through its shared page observer (browser/js/ui/domWatch.js).
import { enhanceNumericInput, enhanceNumericInputs } from './control/numericInput.js';

export const watchNumericInputs = (root = document.body) => {
  enhanceNumericInputs(document);
  const obs = new MutationObserver((records) => {
    for (const rec of records)
      for (const node of rec.addedNodes) {
        if (node.nodeType !== 1) continue;
        if (node.matches?.('input[type="number"]')) enhanceNumericInput(node);
        else enhanceNumericInputs(node);
      }
  });
  obs.observe(root, { childList: true, subtree: true });
  return obs;
};
