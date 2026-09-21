// The hover magnifier over a chat thumbnail: one preview element app-wide, on <body>,
// since the panel clips its own overflow (and the extension popup is 400px wide).
let thumbPreviewEl = null;
export const hideThumbPreview = () => { thumbPreviewEl?.remove(); thumbPreviewEl = null; };
// Switching window never fires mouseleave, so hide on blur. Alt held doubles the glance
// (chat-thumb-preview-xl) and re-places the open preview.
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
    if (e.altKey) box.classList.add('chat-thumb-preview-xl');
    const big = document.createElement('img');
    big.src = img.src;
    big.alt = '';
    box.appendChild(big);
    if (caption) {
      const cap = document.createElement('span');
      cap.className = 'chat-thumb-preview-cap';
      cap.textContent = caption;
      box.appendChild(cap);
    }
    document.body.appendChild(box);
    thumbPreviewEl = box;
    box.__place = () => place(box);
// Measure only once the picture has its size.
    if (big.complete) place(box); else big.addEventListener('load', () => place(box), { once: true });
  });
  img.addEventListener('mouseleave', hideThumbPreview);
  img.addEventListener('click', hideThumbPreview);
  window.addEventListener('scroll', hideThumbPreview, { capture: true });
};
