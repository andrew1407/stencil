import { createActionMenu } from '../lib/actionMenu.js';
import { icon } from '../lib/icons.js';
import { posterImage, editableSrc, pinnable } from '../lib/imageModel.js';
import { listEl, menuEl, run } from './panelDom.js';
import { state, isOpened } from './model.js';
import { openHere, sendToEditor, openCrop, resumeInEditor, openInDesktop, openInTelegram } from './openActions.js';
import { download, togglePin } from './pinActions.js';
import { pinWithPrompt } from './pinDialog.js';

// One shared controller (lib/actionMenu.js) behind the row ⋯ / right-click menus, the
// logo's drag menu placement, and editor mode's per-row menu: the item/sep/label/submenu
// builders, flip-and-clamp placement, and the open/close + Escape machinery.
export const menu = createActionMenu({ menuEl, run });
export const { item, sep, label, submenu } = menu;
export const closeMenu = menu.close;
export const placeMenu = menu.place;

// Fill the menu with the actions for `image`. Editor actions come in pairs: a new tab
// and an in-page modal (▣, mirrors the quick-crop modal), each normal and incognito.
const buildMenu = (image) => {
  menuEl.innerHTML = '';
  // Nested actions Open ▸ / Pin ▸ (plus the flat Crop action). Reused across image, shared,
  // video-frame and poster contexts.
  const editSub = (img) => submenu(icon('pencil', { size: 15 }), 'Open', [
    // "Here" means the editor that is already in front of you: the in-page modal on an
    // ordinary page, the editor tab itself in editor mode (openHere).
    item(icon('monitor', { size: 15 }), state.mode === 'editor' ? 'Into this editor' : 'Here', () => openHere(img, false, undefined, pinAnchor)),
    item(icon('external', { size: 15 }), 'In editor', () => sendToEditor(img, false)),
    item(icon('incognito', { size: 15 }), 'In editor (incognito)', () => sendToEditor(img, true)),
  ]);
  // Crop is a single flat action — open the in-page quick-crop modal ("here"). No submenu,
  // no "in editor" variants.
  const cropItem = (img) => item(icon('crop', { size: 15 }), 'Crop', () => openCrop(img));
  // The dialog anchors to the ⋯ button the menu opened from (the anchor is set BEFORE
  // buildMenu runs, so it is captured at build time; a right-click-opened menu has no
  // button and falls back to the centred dialog).
  const pinAnchor = menu.anchor();
  const pinSub = (img) => submenu(icon('pin', { size: 15 }), img.pinned ? 'Pinned' : 'Pin',
    img.pinned
      ? [item(icon('pin', { size: 15 }), 'Unpin', () => togglePin(img)),
         item(icon('server', { size: 15 }), 'Store on server…', () => pinWithPrompt(img, pinAnchor))]
      : [item(icon('pin', { size: 15 }), 'Locally', () => togglePin(img)),
         item(icon('server', { size: 15 }), 'On server…', () => pinWithPrompt(img, pinAnchor))]);
  // "Open in…" hand-off to another front-end. Desktop app: shown whenever a scheme is
  // configured. Telegram bot: ONLY for a shared (server) row with a bot username — a t.me
  // start payload can't carry image bytes. Returns [] when neither applies (submenu omitted).
  const openInFlat = (img) => {
    const oi = state.openIn || {};
    const children = [];
    if (oi.desktopScheme)
      children.push(item(icon('monitor', { size: 15 }), 'Desktop app', () => openInDesktop(img)));
    if (oi.telegramBotUsername && img.shared && img.serverUrl && img.projectId)
      children.push(item(icon('external', { size: 15 }), 'Telegram bot', () => openInTelegram(img)));
    return children.length ? [submenu(icon('external', { size: 15 }), 'Open in…', children)] : [];
  };

  // Already opened: offer to resume the existing editor (switches to the matching
  // project, or lets the user pick when several share this image) or add a fresh
  // numbered copy. Shown first since it's the point of the yellow badge.
  if (isOpened(image)) {
    const n = image.opened.reduce((a, e) => Math.max(a, e.count || 1), 0);
    menuEl.append(
      item(icon('refresh', { size: 15 }), `Resume in open editor (opened ${n}×)`, () => resumeInEditor(image)),
      item('＋', 'Add as new copy', () => sendToEditor(image, false, 'copy')),
      sep()
    );
  }
  if (image.shared) {
    // A shared (server) row: open / crop the server-stored image. No download/open-in-tab
    // (bytes behind Bearer auth) and no pin (it's already on the server).
    menuEl.append(label('Shared from server'), editSub(image), cropItem(image), ...openInFlat(image));
  } else if (image.kind === 'video') {
    if (image.videoUrl) menuEl.append(
      item(icon('external', { size: 15 }), 'Open video in new tab', () => chrome.tabs.create({ url: image.videoUrl })),
      item(icon('download', { size: 15 }), 'Download video', () => download(image.videoUrl))
    );
    if (editableSrc(image)) {
      if (image.videoUrl) menuEl.append(sep());
      menuEl.append(label('Current frame'), editSub(image), cropItem(image), ...openInFlat(image));
    }
    if (image.posterUrl) {
      const poster = posterImage(image);
      menuEl.append(
        sep(), label('Video preview image'),
        item(icon('external', { size: 15 }), 'Open preview in new tab', () => chrome.tabs.create({ url: poster.src })),
        item(icon('download', { size: 15 }), 'Download preview', () => download(poster.src)),
        editSub(poster), cropItem(poster), ...openInFlat(poster)
      );
    }
    if (pinnable(image)) menuEl.append(sep(), pinSub(image));
  } else {
    menuEl.append(
      item(icon('download', { size: 15 }), 'Download', () => download(image.src)),
      item(icon('external', { size: 15 }), 'Open in new tab', () => chrome.tabs.create({ url: image.src })),
      sep(),
      editSub(image),
      cropItem(image),
      ...openInFlat(image)
    );
    if (pinnable(image)) menuEl.append(pinSub(image));
  }
};

// The image row's ⋯ menu.
export const openMenu = (btn, image) => menu.openAnchored(btn, () => buildMenu(image));
// The shared menu with caller-built items — editor mode's per-row menu.
export const openMenuNodes = menu.openNodes;
// Open the same menu at a point (used by row right-click); no button is anchored.
export const openMenuAt = (image, x, y) => menu.openAt(x, y, () => buildMenu(image));
document.addEventListener('click', (e) => { if (!menuEl.contains(e.target)) closeMenu(); });
listEl.addEventListener('scroll', closeMenu);
