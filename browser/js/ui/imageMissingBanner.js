// The "image too large to save" banner: built lazily above the selection panel, then shown/hidden.

// Both ids open the SAME Open Image modal and controlState swaps which one is shown, so take
// whichever is visible — the pair hotkeyActions' loadImage picks between.
const reupload = () => {
  for (const id of ['open-image-btn', 'load-image-btn']) {
    const btn = document.getElementById(id);
    if (btn && !btn.disabled && btn.offsetParent !== null) return btn.click();
  }
};

export const showImageMissingBanner = (show) => {
  let banner = document.getElementById('image-missing-banner');
  if (!banner) {
    banner = document.createElement('div');
    banner.id = 'image-missing-banner';
    banner.className = 'image-missing-banner';   // presentation: css/components/notifications.css
    banner.innerHTML = '⚠️ <strong>Image too large to save in browser storage.</strong> Your drawing lines are saved. Please re-upload the same image after refreshing — your lines will reappear automatically. <button class="image-missing-reupload">Re-upload Image</button>';
    // Bound here, never as an inline onclick: script-src carries no 'unsafe-inline' (tests/csp.test.js).
    banner.querySelector('.image-missing-reupload').addEventListener('click', () => {
      reupload();
      banner.style.display = 'none';
    });
    const selPanel = document.getElementById('selection-panel');
    selPanel.parentNode.insertBefore(banner, selPanel);
  }
  banner.style.display = show ? 'flex' : 'none';
};

