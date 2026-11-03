// A surface its caller hides at once, kept on screen as a copy where it stood while it leaves: Sliding takes
// its fresh picture while the copy stands in, Transition shrinks the copy back into its anchor (menuToAnchor,
// animations/overlay/overlays.css). Byte twin of browser-extension/src/lib/motion/leaveCopy.js.
export const LEAVE_COPY_CLASS = 'leave-copy';
export const LEAVING_CLASS = 'dd-leaving';
// The exit's longest run before the copy goes even without its animationend (ms).
const LEAVE_FALLBACK_MS = 400;

// A copy of `box` fixed over its client rect, ids dropped and scrolled as it was; null when it shows nothing.
export function holdCopy(box) {
  const r = box?.getBoundingClientRect?.();
  if (!r || !(r.width > 0 && r.height > 0) || typeof document === 'undefined') return null;
  const copy = box.cloneNode(true);
  copy.removeAttribute('id');
  for (const el of copy.querySelectorAll('[id]')) el.removeAttribute('id');
  copy.classList.add(LEAVE_COPY_CLASS);
  Object.assign(copy.style, {
    position: 'fixed', left: `${r.left}px`, top: `${r.top}px`, width: `${r.width}px`, height: `${r.height}px`,
    margin: '0', pointerEvents: 'none',
  });
  document.body.appendChild(copy);
  copy.scrollTop = box.scrollTop;
  return copy;
}

// Transition's exit for a box hidden at once: its copy plays `.dd-leaving` and is removed as that ends.
export function cssAway(box) {
  const copy = holdCopy(box);
  if (!copy) return false;
  copy.classList.add(LEAVING_CLASS);
  let timer = 0;
  const done = () => { clearTimeout(timer); copy.remove(); };
  copy.addEventListener('animationend', done, { once: true });
  timer = setTimeout(done, LEAVE_FALLBACK_MS);
  return true;
}
