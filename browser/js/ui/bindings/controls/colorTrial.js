// A native colour picker whose drag is a trial: each `input` tries the value, and back on the colour
// it opened at the trial ends (null); `change` commits once. The page's next touch re-reads the
// value, so a pick the browser reverted without an `input` still ends its trial.
import { anchorPickerInput } from '../../../utils.js';
import { dismissTip } from '../../tip/controlTooltip.js';

const TOUCHES = Object.freeze(['pointerdown', 'keydown']);

// A field hoisted to <body> goes home on the commit or the page's next touch, both after the
// picker has closed, so the box it hangs from never moves under it.
const homeAfterPick = (input, restore) => {
  const done = () => {
    input.removeEventListener('change', done);
    for (const t of TOUCHES) document.removeEventListener(t, done, true);
    restore();
  };
  input.addEventListener('change', done);
  for (const t of TOUCHES) document.addEventListener(t, done, true);
};

// Every native picker opens through here: laid over `anchor`, with that control's tip dropped.
export const openColorPicker = (input, anchor) => {
  const restore = anchor ? anchorPickerInput(input, anchor) : null;
  if (restore) homeAfterPick(input, restore);
  dismissTip();
  try {
    if (typeof input.showPicker === 'function') input.showPicker();
    else input.click();
  } catch {
    input.click();
  }
};

export const colorTrial = (input, { onTry, onCommit }) => {
  let from = null;
  const tryNow = () => onTry(input.value === from ? null : input.value);
  const touched = () => { disarm(); tryNow(); };
  const disarm = () => { for (const t of TOUCHES) document.removeEventListener(t, touched, true); };
  input.addEventListener('input', tryNow);
  input.addEventListener('change', () => { disarm(); onCommit(input.value); });
  return (anchor) => {
    from = input.value;
    openColorPicker(input, anchor);
    disarm();
    for (const t of TOUCHES) document.addEventListener(t, touched, true);
  };
};
