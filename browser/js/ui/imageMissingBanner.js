// ── "Image too large to save" banner ────────────────────────────
// Extracted from storage.js — the one piece of it that paints. Built lazily above the
// selection panel and then only shown/hidden.

export const showImageMissingBanner = (show) => {
  let banner = document.getElementById('image-missing-banner');
  if (!banner) {
    banner = document.createElement('div');
    banner.id = 'image-missing-banner';
    banner.className = 'image-missing-banner';   // presentation: css/components/notifications.css
    banner.innerHTML = '⚠️ <strong>Image too large to save in browser storage.</strong> Your drawing lines are saved. Please re-upload the same image after refreshing — your lines will reappear automatically. <button class="image-missing-reupload" onclick="document.getElementById(\'imageUpload\').click();this.closest(\'div\').style.display=\'none\'">Re-upload Image</button>';
    const selPanel = document.getElementById('selection-panel');
    selPanel.parentNode.insertBefore(banner, selPanel);
  }
  banner.style.display = show ? 'flex' : 'none';
};

