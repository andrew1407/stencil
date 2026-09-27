// A native colour picker whose drag is a trial: each `input` tries the value, and back on the colour
// it opened at the trial ends (null); `change` commits once. The page's next touch re-reads the
// value, so a pick the browser reverted without an `input` still ends its trial.
import { anchorPickerInput } from '../../../utils.js';

const TOUCHES = Object.freeze(['pointerdown', 'keydown']);

export const colorTrial = (input, { onTry, onCommit }) => {
  let from = null;
  const tryNow = () => onTry(input.value === from ? null : input.value);
  const touched = () => { disarm(); tryNow(); };
  const disarm = () => { for (const t of TOUCHES) document.removeEventListener(t, touched, true); };
  input.addEventListener('input', tryNow);
  input.addEventListener('change', () => { disarm(); onCommit(input.value); });
  return (anchor) => {
    from = input.value;
    anchorPickerInput(input, anchor);
    try {
      if (typeof input.showPicker === 'function') input.showPicker();
      else input.click();
    } catch {
      input.click();
    }
    disarm();
    for (const t of TOUCHES) document.addEventListener(t, touched, true);
  };
};
