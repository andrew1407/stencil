// The composer's queued-attachment chips; whoever mutates attachments fires
// CHAT_ATTACHMENTS_EVENT so both surfaces repaint.
import { ATTACH_SETTLE_STEP_MS, ATTACH_SETTLE_TRIES, chipLeave } from './chatLeave.js';
import { CHAT_ATTACHMENTS_EVENT } from '../../llm/chat/chatController.js';
import { LEAVING_CLASS, wipeDurationMs } from '../motion.js';
import { icon } from '../icons.js';
import { publish } from '../../eventBus/appBus.js';
import { wireThumbPreview } from './chatThumbPreview.js';

export { CHAT_ATTACHMENTS_EVENT };
export const notifyAttachmentsChanged = () => publish(CHAT_ATTACHMENTS_EVENT);

// Chips are keyed by the attachment object and patched in place, never rebuilt: a
// rebuild deletes the disintegrate layer mid-flight. One chip node per container.
const CHIPS_BY_CONTAINER = new WeakMap();
export const chatAttachmentChips = (container, controller) => {
  let CHIP_FOR = CHIPS_BY_CONTAINER.get(container);
  if (!CHIP_FOR) { CHIP_FOR = new WeakMap(); CHIPS_BY_CONTAINER.set(container, CHIP_FOR); }
  const list = controller ? controller.attachments : [];
  for (let i = 0; i < list.length; i++) {
    const at = list[i];
    let chip = CHIP_FOR.get(at);
// Re-inserting a live node restarts its CSS animations; the queue only appends.
    if (chip && chip.isConnected && !chip.classList.contains(LEAVING_CLASS)) continue;
    chip = document.createElement('span');
    chip.className = 'chat-attach-chip';
    CHIP_FOR.set(at, chip);
// The picture itself; hovering magnifies it (wireThumbPreview).
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
    name.dataset.title = label;
    const rm = document.createElement('button');
    rm.className = 'chat-hbtn chat-attach-remove';
    rm.setAttribute('aria-label', 'Remove attachment');
    rm.innerHTML = icon('x', { size: 12 });
// The chip scatters before the queue drops it.
    rm.addEventListener('click', () => chipLeave(chip, () => {
      const at2 = controller.attachments.indexOf(at);
      if (at2 >= 0) controller.removeAttachment(at2);
      chip.remove();
      notifyAttachmentsChanged();
    }));
    chip.append(name, rm);
    container.appendChild(chip);
  }
// Chips whose attachment is gone leave the same way; the dust layer owns its own lifetime.
  for (const el of [...container.children]) {
    if (el.classList.contains('disintegrate-host') || el.classList.contains(LEAVING_CLASS)) continue;
    const still = list.some((at) => CHIP_FOR.get(at) === el);
    if (!still) chipLeave(el, () => el.remove());
  }
// The dust layer lives in this container, so an empty queue hides the row only once
// nothing is animating — polled, bounded, since a mote layer outlives wipeDurationMs.
  const animating = () => [...container.children].some((el) =>
    el.classList.contains('disintegrate-host') || el.classList.contains(LEAVING_CLASS));
  const hideWhenSettled = (wait, tries) => setTimeout(() => {
    if (controller?.attachments?.length) return;
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
