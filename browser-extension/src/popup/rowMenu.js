import { createActionMenu } from '../lib/control/actionMenu.js';
import { icon } from '../lib/icons.js';
import { posterImage, editableSrc, pinnable } from '../lib/image/imageModel.js';
import { listEl, menuEl, run } from './panelDom.js';
import { state, isOpened } from './model.js';
import { openHere, sendToEditor, openCrop, resumeInEditor, openInDesktop, openInTelegram } from './openActions.js';
import { download, togglePin } from './pinActions.js';
import { pinWithPrompt } from './pinDialog.js';

// One shared controller behind the row menus, the logo's drag menu and editor mode's rows.
export const menu = createActionMenu({ menuEl, run });
export const { item, sep, label, submenu } = menu;
export const closeMenu = menu.close;
export const placeMenu = menu.place;

const buildMenu = (image) => {
  menuEl.innerHTML = '';
  const editSub = (img) => submenu(icon('pencil', { size: 15 }), 'Open', [
    item(icon('monitor', { size: 15 }), state.mode === 'editor' ? 'Into this editor' : 'Here', () => openHere(img, false, undefined, pinAnchor)),
    item(icon('external', { size: 15 }), 'In editor', () => sendToEditor(img, false)),
    item(icon('incognito', { size: 15 }), 'In editor (incognito)', () => sendToEditor(img, true)),
  ]);
  const cropItem = (img) => item(icon('crop', { size: 15 }), 'Crop', () => openCrop(img));
  // Captured at build time; a right-click-opened menu has no button (centred dialog).
  const pinAnchor = menu.anchor();
  const pinSub = (img) => submenu(icon('pin', { size: 15 }), img.pinned ? 'Pinned' : 'Pin',
    img.pinned
      ? [item(icon('pin', { size: 15 }), 'Unpin', () => togglePin(img)),
         item(icon('server', { size: 15 }), 'Store on server…', () => pinWithPrompt(img, pinAnchor))]
      : [item(icon('pin', { size: 15 }), 'Locally', () => togglePin(img)),
         item(icon('server', { size: 15 }), 'On server…', () => pinWithPrompt(img, pinAnchor))]);
  // Telegram only for a shared row: a t.me start payload cannot carry image bytes.
  const openInFlat = (img) => {
    const oi = state.openIn || {};
    const children = [];
    if (oi.desktopScheme)
      children.push(item(icon('monitor', { size: 15 }), 'Desktop app', () => openInDesktop(img)));
    if (oi.telegramBotUsername && img.shared && img.serverUrl && img.projectId)
      children.push(item(icon('external', { size: 15 }), 'Telegram bot', () => openInTelegram(img)));
    return children.length ? [submenu(icon('external', { size: 15 }), 'Open in…', children)] : [];
  };

  if (isOpened(image)) {
    const n = image.opened.reduce((a, e) => Math.max(a, e.count || 1), 0);
    menuEl.append(
      item(icon('refresh', { size: 15 }), `Resume in open editor (opened ${n}×)`, () => resumeInEditor(image)),
      item('＋', 'Add as new copy', () => sendToEditor(image, false, 'copy')),
      sep()
    );
  }
  if (image.shared) {
    // No download / open-in-tab: the bytes sit behind Bearer auth.
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

export const openMenu = (btn, image) => menu.openAnchored(btn, () => buildMenu(image));
export const openMenuNodes = menu.openNodes;
export const openMenuAt = (image, x, y) => menu.openAt(x, y, () => buildMenu(image));
document.addEventListener('click', (e) => { if (!menuEl.contains(e.target)) closeMenu(); });
listEl.addEventListener('scroll', closeMenu);
