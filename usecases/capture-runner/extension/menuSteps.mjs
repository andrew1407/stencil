// Chrome's right-click menu on a picture and on the clip, its Stencil entry open. Chrome draws that
// menu natively, out of reach of any screenshot, so the shot draws it over the demo site: Chrome's
// own items from config `chromeMenu`, the Stencil ones from the worker's table (visibleMenu).
import { pointerOnPage } from '../lib/shot/pointer.mjs';
import { TIMEOUTS, VIEWS, config, host, runner } from './session.mjs';
import { popup } from './popup.mjs';

// The items Chrome shows for `context`: the poster group when the video has one, the desktop
// items when a desktop scheme is set — the two reveals the worker makes.
const visibleItems = (ui, context, poster) => ui.evaluate(async ([kind, hasPoster]) => {
  const { MENU_ITEMS, PREVIEW_ITEMS, STATIC_DESKTOP_ITEMS, visibleMenu } = await import('/src/lib/menu/contextMenu.js');
  const { getSettings } = await import('/src/lib/stencil.js');
  const { desktopScheme } = await getSettings();
  const ids = visibleMenu(kind, [...(hasPoster ? PREVIEW_ITEMS : []), ...(desktopScheme ? STATIC_DESKTOP_ITEMS : [])]);
  return MENU_ITEMS.filter((item) => ids.includes(item.id)).map(({ id, parentId, title }) => ({ id, parentId, title }));
}, [context, poster]);

// One row list per open menu, root first: `open` is the chain of Stencil ids whose submenus show,
// and the config's "-" is a separator, "@stencil" the extension's own entry.
const menuLevels = (chrome, items, open) => {
  const parent = (item) => items.some((other) => other.parentId === item.id);
  const root = items.find((item) => !item.parentId);
  const top = chrome.map((entry) => {
    if (entry === '-') return { sep: true };
    if (entry === '@stencil') return { title: root.title, sub: true, lit: open[0] === root.id, icon: true };
    return typeof entry === 'string' ? { title: entry } : { title: entry.label, sub: !!entry.sub };
  });
  return [top, ...open.map((id, depth) => {
    const level = items.filter((item) => item.parentId === id);
    if (!level.length) throw new Error(`no menu items under ${id}`);
    return level.map((item) => ({ title: item.title, sub: parent(item), lit: item.id === open[depth + 1] }));
  })];
};

// Runs IN the page: macOS menu metrics — 24px rows, a blurred translucent panel, the open item in
// the system blue; a submenu opens beside its row, and once it meets the edge every next one opens left.
const drawMenu = ([levels, at, dark, icon]) => {
  const C = dark ? { bg: 'rgba(36,36,38,.78)', fg: 'rgba(255,255,255,.9)', edge: 'rgba(255,255,255,.16)', sep: 'rgba(255,255,255,.14)' }
    : { bg: 'rgba(246,246,246,.82)', fg: 'rgba(0,0,0,.86)', edge: 'rgba(0,0,0,.12)', sep: 'rgba(0,0,0,.1)' };
  const layer = document.createElement('div');
  layer.id = 'docs-ctx-menu';
  layer.style.cssText = 'position:fixed;inset:0;z-index:2147483646;pointer-events:none;'
    + 'font:13px -apple-system,system-ui,"Segoe UI","Apple Color Emoji","Segoe UI Emoji","Noto Color Emoji",sans-serif';
  document.body.append(layer);
  const chevron = '<svg width="7" height="12" viewBox="0 0 7 12" fill="none" stroke="currentColor" stroke-width="1.6" '
    + 'stroke-linecap="round" stroke-linejoin="round"><path d="M1.2 1.2 5.8 6l-4.6 4.8"/></svg>';
  const row = (item) => {
    const el = document.createElement('div');
    if (item.sep) {
      el.style.cssText = `height:1px;margin:5px 10px;background:${C.sep}`;
      return el;
    }
    el.style.cssText = `display:flex;align-items:center;height:24px;padding:0 12px 0 ${item.icon ? 6 : 19}px;border-radius:5px;`
      + `white-space:nowrap;${item.lit ? 'background:#0a64d8;color:#fff' : ''}`;
    if (item.icon) el.append(Object.assign(new Image(16, 16), { src: icon, style: 'margin-right:7px' }));
    const title = document.createElement('span');
    title.style.flex = '1';
    title.textContent = item.title;
    el.append(title);
    if (item.sub) el.insertAdjacentHTML('beforeend', `<span style="margin-left:32px;display:flex;opacity:.85">${chevron}</span>`);
    return el;
  };
  let anchor = { left: at[0], right: at[0], top: at[1] };
  let leftward = false;
  let hover = at;
  for (const level of levels) {
    const box = document.createElement('div');
    box.style.cssText = `position:fixed;min-width:190px;padding:5px;background:${C.bg};color:${C.fg};`
      + 'backdrop-filter:blur(26px) saturate(1.7);border-radius:10px;'
      + `box-shadow:inset 0 0 0 1px ${C.edge},0 0 0 1px rgba(0,0,0,.55),0 12px 34px rgba(0,0,0,.45)`;
    const rows = level.map(row);
    box.append(...rows);
    layer.append(box);
    const { width, height } = box.getBoundingClientRect();
    leftward = leftward || anchor.right + width > innerWidth;
    box.style.left = `${Math.max(0, leftward ? anchor.left - width : anchor.right)}px`;
    box.style.top = `${Math.max(0, Math.min(anchor.top - 5, innerHeight - height - 4))}px`;
    const lit = rows[level.findIndex((item) => item.lit)];
    if (lit) {
      const r = lit.getBoundingClientRect();
      hover = [Math.round(r.left + r.width * 0.72), Math.round(r.top + r.height * 0.55)];
    }
    const placed = box.getBoundingClientRect();
    if (lit) anchor = { left: placed.left + 3, right: placed.right - 3, top: lit.getBoundingClientRect().top };
  }
  const boxes = [...layer.children].map((box) => box.getBoundingClientRect());
  return { hover, left: Math.min(...boxes.map((b) => b.left)), top: Math.min(...boxes.map((b) => b.top)),
    right: Math.max(...boxes.map((b) => b.right)), bottom: Math.max(...boxes.map((b) => b.bottom)) };
};

// CSS px of page kept around the menus and the pointer, so the shot shows what was right-clicked.
const MARGIN = 28;
const around = ({ left, top, right, bottom }, { width, height }) => {
  const x = Math.max(0, Math.floor(left - MARGIN));
  const y = Math.max(0, Math.floor(top - MARGIN));
  return { clip: { x, y, width: Math.min(width, Math.ceil(right + MARGIN)) - x, height: Math.min(height, Math.ceil(bottom + MARGIN)) - y } };
};

const menuShot = (name, { context, target, open, poster = false }) => ({ name, run: async (ctx, theme) => {
  const levels = menuLevels(config.get(`chromeMenu.${context}`), await visibleItems(await popup(ctx, theme), context, poster), open);
  // Chrome's menu is tall: the crop-modal height keeps its open submenus inside the shot.
  await host.setViewportSize(VIEWS.cropModal);
  await host.bringToFront();
  const box = await host.locator(target).boundingBox();
  const at = [Math.round(box.x + box.width * 0.3), Math.round(box.y + box.height * 0.15)];
  await pointerOnPage(host);
  const span = await host.evaluate(drawMenu, [levels, at, theme === 'dark', '/browser-extension/icons/icon-16.png']);
  // The pointer rests on the deepest open row: the one whose submenu it opened.
  await host.evaluate(([x, y]) => window.__docsPointer.pin(x, y), span.hover);
  await host.waitForFunction(() => [...document.querySelectorAll('#docs-ctx-menu img')].every((img) => img.complete),
    null, { timeout: TIMEOUTS.scanMs });
  await runner.shot(host, name, around(span, VIEWS.cropModal));
  await host.evaluate(() => {
    document.getElementById('docs-ctx-menu').remove();
    window.__docsPointer.hide();
  });
  await host.setViewportSize(VIEWS.site);
} });

export const makeMenuSteps = () => Object.freeze([
  menuShot('site-menu-image', { context: 'image', target: 'img[alt="Plate 2"]', open: ['stencil-root', 'stencil-open-parent'] }),
  menuShot('site-menu-video', { context: 'video', target: '#clip', open: ['stencil-root'], poster: true }),
]);
