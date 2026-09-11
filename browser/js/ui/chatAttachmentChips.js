// ── The composer's queued-attachment chips ──────────────────────
// Both surfaces render the SAME shared controller's queue, so a file queued in one appears
// in the other: whoever mutates attachments fires CHAT_ATTACHMENTS_EVENT.
import { ATTACH_SETTLE_STEP_MS, ATTACH_SETTLE_TRIES, chipLeave } from './chatLeave.js';
import { CHAT_ATTACHMENTS_EVENT } from '../llm/chatController.js';
import { LEAVING_CLASS, wipeDurationMs } from './motion.js';
import { icon } from './icons.js';
import { publish } from '../bus/appBus.js';
import { wireThumbPreview } from './chatThumbPreview.js';

export { CHAT_ATTACHMENTS_EVENT };
export const notifyAttachmentsChanged = () => publish(CHAT_ATTACHMENTS_EVENT);

// The pending-attachment chips, shared by the panel and the context-menu composer;
// `controller` may be null (nothing queued — the row hides). Chips are keyed by the
// ATTACHMENT OBJECT and the row is patched in place, never rebuilt: a wholesale
// `innerHTML = ''` deleted the disintegrate layer mid-flight and replayed the
// survivors' entrances. PER CONTAINER: one chip node can only live in one list.
const CHIPS_BY_CONTAINER = new WeakMap();
export const chatAttachmentChips = (container, controller) => {
  let CHIP_FOR = CHIPS_BY_CONTAINER.get(container);
  if (!CHIP_FOR) { CHIP_FOR = new WeakMap(); CHIPS_BY_CONTAINER.set(container, CHIP_FOR); }
  const list = controller ? controller.attachments : [];
  for (let i = 0; i < list.length; i++) {
    const at = list[i];
    let chip = CHIP_FOR.get(at);
    // Already on screen: leave it exactly where it is — re-inserting a live node
    // RESTARTS its CSS animations. The queue only appends, so the order is right.
    if (chip && chip.isConnected && !chip.classList.contains(LEAVING_CLASS)) continue;
    chip = document.createElement('span');
    chip.className = 'chat-attach-chip';
    CHIP_FOR.set(at, chip);
    // The queued picture itself, small — hovering magnifies it (wireThumbPreview).
    // A name alone said nothing about WHICH image was queued, and a data: URL's name
    // is a wall of base64 (see fileNameForUrl) that reads as garbage in the chip.
    const label = at.kind === 'video' ? `${at.name} (${(at.frames || []).length} frames)` : at.name;
    const src = at.dataUrl || at.frames?.[0] || '';
    if (src) {
      const thumb = document.createElement('img');
      thumb.className = 'chat-attach-thumb';
      thumb.src = src;
      thumb.alt = at.name;
      wireThumbPreview(thumb, label);
      chip.appendChild(thumb);
    }
    const name = document.createElement('span');
    name.className = 'chat-attach-name';
    name.textContent = label;
    // The name is ellipsised in CSS, so the full one lives on the tooltip.
    name.dataset.title = label;
    const rm = document.createElement('button');
    rm.className = 'chat-hbtn chat-attach-remove';
    rm.setAttribute('aria-label', 'Remove attachment');   // no tooltip — the × says it
    rm.innerHTML = icon('x', { size: 12 });
    // The chip scatters before the queue drops it — and the row is patched, not
    // rebuilt, so the dust keeps flying over the chips that stay.
    rm.addEventListener('click', () => chipLeave(chip, () => {
      const at2 = controller.attachments.indexOf(at);
      if (at2 >= 0) controller.removeAttachment(at2);
      chip.remove();
      notifyAttachmentsChanged();
    }));
    chip.append(name, rm);
    container.appendChild(chip);
  }
  // Anything whose attachment is gone leaves the same way (unless it is already on
  // its way out, or it is the disintegrate layer, which owns its own lifetime).
  for (const el of [...container.children]) {
    if (el.classList.contains('disintegrate-host') || el.classList.contains(LEAVING_CLASS)) continue;
    const still = list.some((at) => CHIP_FOR.get(at) === el);
    if (!still) chipLeave(el, () => el.remove());
  }
  // The dust layer lives IN this container (motion.js appends to the parent), so an
  // empty queue must not hide the row while particles are still flying. Hide only
  // once nothing is animating — and KEEP looking until that is true: a mote layer
  // outlives the wipe's own clock by the grace disintegrate gives it, so a single look
  // at wipeDurationMs can land while the cloud is still there and leave the row open
  // for good. Bounded, so a stranded layer can never hold it open forever either.
  const animating = () => [...container.children].some((el) =>
    el.classList.contains('disintegrate-host') || el.classList.contains(LEAVING_CLASS));
  const hideWhenSettled = (wait, tries) => setTimeout(() => {
    if (controller?.attachments?.length) return;   // something was queued again
    if (animating()) { if (tries > 0) hideWhenSettled(ATTACH_SETTLE_STEP_MS, tries - 1); return; }
    container.style.display = 'none';
  }, wait);
  if (list.length || animating()) {
    container.style.display = '';
    if (!list.length) hideWhenSettled(wipeDurationMs() + 50, ATTACH_SETTLE_TRIES);
  } else {
    container.style.display = 'none';
  }
};
