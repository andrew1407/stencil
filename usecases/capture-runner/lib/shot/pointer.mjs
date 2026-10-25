// A drawn pointer for clips: a screencast or a debugger-attached app paints no cursor, so the
// page under the camera gets an arrow that follows mouse, drag and touch input, a ring on each
// press and a fingertip for touch. Installed on every document a context loads, or on a live page.

// Runs IN the page; everything it needs is inside it.
const install = () => {
  if (window.__docsPointer) return;
  const SVG = 'http://www.w3.org/2000/svg';
  const host = document.createElement('div');
  host.style.cssText = 'position:fixed;left:0;top:0;width:0;height:0;z-index:2147483647;pointer-events:none;display:none';
  const arrow = document.createElementNS(SVG, 'svg');
  arrow.setAttribute('width', '22');
  arrow.setAttribute('height', '30');
  arrow.style.cssText = 'position:absolute;left:-2px;top:-2px;overflow:visible;filter:drop-shadow(0 1px 1.5px rgba(0,0,0,.45))';
  const shape = document.createElementNS(SVG, 'path');
  shape.setAttribute('d', 'M2 2 L2 24 L8 18.5 L12 27.5 L15.5 26 L11.6 17.2 L19.5 17.2 Z');
  shape.setAttribute('fill', '#ffffff');
  shape.setAttribute('stroke', '#111111');
  shape.setAttribute('stroke-width', '1.6');
  shape.setAttribute('stroke-linejoin', 'round');
  arrow.append(shape);
  const tip = document.createElement('div');
  tip.style.cssText = 'position:absolute;left:-17px;top:-17px;width:34px;height:34px;border-radius:50%;'
    + 'background:rgba(255,255,255,.38);border:2px solid rgba(255,255,255,.92);box-shadow:0 0 0 1px rgba(0,0,0,.35);display:none';
  // A drag's image rides under the arrow, its grab point on the hotspot, as the OS draws it.
  const carried = document.createElement('img');
  carried.style.cssText = 'position:absolute;display:none;pointer-events:none';
  host.append(carried, arrow, tip);
  const root = () => document.body || document.documentElement;
  // Kept the body's last child, so an overlay mounted after it never covers the pointer.
  const mount = () => { if (host !== root().lastElementChild) root().append(host); };
  if (document.body) mount(); else addEventListener('DOMContentLoaded', mount, { once: true });

  let pinned = false;
  const place = (x, y, touch = false) => {
    if (pinned) return;
    mount();
    arrow.style.display = touch ? 'none' : '';
    tip.style.display = touch ? 'block' : 'none';
    host.style.display = 'block';
    host.style.transform = `translate(${x}px, ${y}px)`;
  };
  // A press shows as a ring that grows and fades from the hotspot.
  const ring = (x, y) => {
    const r = document.createElement('div');
    r.style.cssText = `position:fixed;left:${x - 15}px;top:${y - 15}px;width:30px;height:30px;border-radius:50%;`
      + 'border:2px solid rgba(255,255,255,.95);box-shadow:0 0 0 1px rgba(0,0,0,.4);z-index:2147483646;pointer-events:none';
    root().append(r);
    r.animate([{ transform: 'scale(.35)', opacity: 1 }, { transform: 'scale(1.25)', opacity: 0 }],
      { duration: 420, easing: 'ease-out' }).onfinish = () => r.remove();
  };
  // A clip that starts on a touch screen hides the arrow a setup click left behind.
  window.__docsPointer = {
    hide: () => { pinned = false; host.style.display = 'none'; },
    // A still's pointer: held at (x, y) until hide(), whatever moves the page sees meanwhile.
    pin: (x, y) => { pinned = false; place(x, y); pinned = true; },
    // `src` (null drops it) is drawn `width` CSS px wide, grabbed at (dx, dy) of its own box.
    carry: (src, dx = 0, dy = 0, width = 0) => {
      carried.style.display = src ? 'block' : 'none';
      if (!src) return;
      carried.src = src;
      Object.assign(carried.style, { left: `${-dx}px`, top: `${-dy}px`, width: `${width}px` });
    },
  };
  const on = (type, fn) => addEventListener(type, fn, { capture: true, passive: true });
  on('mousemove', (e) => place(e.clientX, e.clientY));
  on('pointermove', (e) => place(e.clientX, e.clientY, e.pointerType === 'touch'));
  on('dragover', (e) => place(e.clientX, e.clientY));
  on('pointerdown', (e) => { if (e.pointerType !== 'touch') ring(e.clientX, e.clientY); });
  on('touchstart', (e) => { const t = e.touches[0]; if (t) place(t.clientX, t.clientY, true); });
  on('touchmove', (e) => { const t = e.touches[0]; if (t) place(t.clientX, t.clientY, true); });
  on('touchend', () => { host.style.display = 'none'; });
};

// Every document the context (or page) loads from now on.
export const pointerOnLoad = (target) => target.addInitScript(install);

// A page that is already open, such as a debugger-attached app.
export const pointerOnPage = (page) => page.evaluate(install);
