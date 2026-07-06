// ── Toolbar (action) icon recolouring ───────────────────────────────────────
// Tints the toolbar icon's outline to the selected accent — the way the browser
// favicon follows the theme. Rendered in the service worker (no DOM) by drawing the
// Stencil badge with OffscreenCanvas 2D paths (mirrors browser/favicon.svg +
// extension/icons/icon.svg) → ImageData → chrome.action.setIcon. Re-applied whenever
// the accent changes (it's mirrored to chrome.storage.local by lib/accent.js).
// Best-effort: any failure leaves the static manifest PNGs in place.
import { ACCENT_HEX, DEFAULT_HL, ACCENT_STORAGE_KEY } from './highlightColor.js';

const SIZES = [16, 32, 48];

const PANEL = '#2b2f3a';
const FRAME = '#3a3f4b';
const YELLOW = '#FFFF00';
const MARKERS = [[16, 46], [27, 24], [38, 38], [50, 18]]; // polyline vertices = dot centres

// Draw the badge into `ctx` sized `size` (viewBox is 64), tinting the panel border to
// `accent`. Corners outside the rounded panel stay transparent.
const drawBadge = (ctx, size, accent) => {
  const k = size / 64; // viewBox unit → pixels
  const u = (v) => v * k;
  const rrect = (x, y, w, h, r) => {
    ctx.beginPath();
    ctx.roundRect(u(x), u(y), u(w), u(h), u(r));
  };
  ctx.clearRect(0, 0, size, size);
  rrect(2, 2, 60, 60, 13);
  ctx.fillStyle = PANEL;
  ctx.fill();
  // Thick accent ring (width 4 on the 64 grid → visible even at 16px), its outer edge
  // aligned to the panel edge at 2.
  rrect(4, 4, 56, 56, 11);
  ctx.lineWidth = u(4);
  ctx.strokeStyle = accent;
  ctx.stroke();
  rrect(12, 12, 40, 40, 4);
  ctx.fillStyle = FRAME;
  ctx.fill();
  ctx.beginPath();
  MARKERS.forEach(([cx, cy], i) => (i ? ctx.lineTo(u(cx), u(cy)) : ctx.moveTo(u(cx), u(cy))));
  ctx.strokeStyle = YELLOW;
  ctx.lineWidth = u(3.5);
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  ctx.stroke();
  for (const [cx, cy] of MARKERS) {
    ctx.beginPath();
    ctx.arc(u(cx), u(cy), u(3.4), 0, Math.PI * 2);
    ctx.fillStyle = YELLOW;
    ctx.fill();
    ctx.lineWidth = u(1.25);
    ctx.strokeStyle = '#000000';
    ctx.stroke();
  }
};

// Render one size at 4× then downscale, for clean antialiased edges at 16/32px.
const imageDataFor = (size, accent) => {
  const ss = size * 4;
  const big = new OffscreenCanvas(ss, ss);
  drawBadge(big.getContext('2d'), ss, accent);
  const small = new OffscreenCanvas(size, size);
  const ctx = small.getContext('2d');
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = 'high';
  ctx.clearRect(0, 0, size, size);
  ctx.drawImage(big, 0, 0, size, size);
  return ctx.getImageData(0, 0, size, size);
};

const currentAccentHex = async () => {
  try {
    const got = await chrome.storage.local.get(ACCENT_STORAGE_KEY);
    return ACCENT_HEX[got[ACCENT_STORAGE_KEY]] || DEFAULT_HL;
  } catch {
    return DEFAULT_HL;
  }
};

// Render the accent-bordered badge and install it as the toolbar action icon.
export const applyAccentActionIcon = async () => {
  try {
    const accent = await currentAccentHex();
    const imageData = {};
    for (const s of SIZES) imageData[s] = imageDataFor(s, accent);
    await chrome.action.setIcon({ imageData });
  } catch (e) {
    console.warn('[stencil] could not recolour the toolbar icon:', e?.message);
  }
};

// Re-tint the toolbar icon whenever the accent changes (mirrored to storage.local).
export const watchAccentActionIcon = () => {
  chrome.storage.onChanged.addListener((changes, area) => {
    if (area === 'local' && changes[ACCENT_STORAGE_KEY]) applyAccentActionIcon();
  });
};
