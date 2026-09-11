// ── The hover magnifier over a chat thumbnail ───────────────────
// ONE preview element app-wide, parked on <body> so no transcript clip can cut it.


// The images the user attached to one turn, as a thumbnail strip above their message.
// `name` is a filename — untrusted, so it rides `alt`/`title` as data, never markup. A
// video shows its first sampled frame — frames are what the model sees (contract §7).
// The hover preview is fixed-position on the BODY: the panel clips its own overflow
// (and the extension popup is 400px wide), so an in-place popup would be cut off.
let thumbPreviewEl = null;
export const hideThumbPreview = () => { thumbPreviewEl?.remove(); thumbPreviewEl = null; };
// Switching window never fires the thumbnail's mouseleave — hide on blur, module-wide.
// Alt HELD doubles the glance (chat-thumb-preview-xl); pressed or released mid-hover
// it resizes in place and the open preview re-places itself to stay in view.
if (typeof window !== 'undefined') {
  window.addEventListener('blur', hideThumbPreview);
  const altPreview = (on) => {
    if (!thumbPreviewEl) return;
    thumbPreviewEl.classList.toggle('chat-thumb-preview-xl', on);
    thumbPreviewEl.__place?.();
  };
  window.addEventListener('keydown', (e) => { if (e.key === 'Alt') altPreview(true); });
  window.addEventListener('keyup', (e) => { if (e.key === 'Alt') altPreview(false); });
}
export const wireThumbPreview = (img, caption = '') => {
  const place = (box) => {
    const r = img.getBoundingClientRect();
    const b = box.getBoundingClientRect();
    const left = Math.max(8, Math.min(r.left, window.innerWidth - b.width - 8));
    // Above the thumbnail by default; below when there isn't room up there.
    const above = r.top - b.height - 10;
    const top = above >= 8 ? above : Math.min(r.bottom + 10, window.innerHeight - b.height - 8);
    box.style.left = `${Math.round(left)}px`;
    box.style.top = `${Math.round(Math.max(8, top))}px`;
    box.classList.add('chat-thumb-preview-in');
  };
  img.addEventListener('mouseenter', (e) => {
    hideThumbPreview();
    const box = document.createElement('div');
    box.className = 'chat-thumb-preview';
    if (e.altKey) box.classList.add('chat-thumb-preview-xl');   // Alt already held on entry
    const big = document.createElement('img');
    big.src = img.src;
    big.alt = '';
    box.appendChild(big);
    if (caption) {
      const cap = document.createElement('span');
      cap.className = 'chat-thumb-preview-cap';
      cap.textContent = caption;   // a filename — data, like everywhere else
      box.appendChild(cap);
    }
    document.body.appendChild(box);
    thumbPreviewEl = box;
    box.__place = () => place(box);   // the Alt resize re-places the open box
    // Measure only once the picture has its size, or the flip decides on an empty box.
    if (big.complete) place(box); else big.addEventListener('load', () => place(box), { once: true });
  });
  img.addEventListener('mouseleave', hideThumbPreview);
  // A scroll (or a click anywhere) moves the anchor out from under the preview.
  img.addEventListener('click', hideThumbPreview);
  window.addEventListener('scroll', hideThumbPreview, { capture: true });
};
