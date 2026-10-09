// The assistant's provider status: a cheap probe on open / after a settings change, shown as the
// dot on the "…" trigger plus its themed tooltip. Never blocks sending. Split out of panel.js.
import { probeProvider } from '../../llm/client.js';
import { loadLlmSettings, serverBearerToken, withSessionKey } from '../../llm/settings.js';
import { cacheProbe, probeStatusClass } from '../../llm/chat/session.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';
import { surfaceIn, surfaceOut, settleSurface, rectCenter,
         TIP_DUST_IN_MS, TIP_DUST_OUT_MS } from '../motion.js';
import { gearStatusRows, gearTipFootText } from './geometry.js';

// The status table a "…" trigger shows on hover or keyboard focus, fixed above it like #app-tooltip:
// `probe()` is read at every show, and `refresh()` re-renders it in place while it is up.
export function wireStatusTip(statusHost, probe) {
  if (!statusHost?.addEventListener) return { show() {}, hide() {}, refresh() {} };
  const gearTip = document.createElement('div');
  gearTip.className = 'chat-status-tip';
  document.body.appendChild(gearTip);
  const renderGearTip = () => {
    const now = probe();
    gearTip.textContent = '';
    const table = document.createElement('table');
    for (const r of gearStatusRows(now)) {
      const tr = document.createElement('tr');
      const th = document.createElement('th');
      th.textContent = r.label;
      const td = document.createElement('td');
      td.textContent = r.value;
      if (r.state) td.classList.add(`chat-status-${r.state}`);
      tr.append(th, td);
      table.appendChild(tr);
    }
    const foot = document.createElement('div');
    foot.className = 'chat-status-tip-foot';
    foot.textContent = gearTipFootText(now);
    gearTip.append(table, foot);
  };
  const placeGearTip = () => {
    const r = statusHost.getBoundingClientRect();
    const t = gearTip.getBoundingClientRect();
    const pad = 8;
    const x = Math.max(pad, Math.min(r.left + r.width / 2 - t.width / 2, window.innerWidth - t.width - pad));
    const above = r.top - t.height - pad;
    gearTip.style.left = `${Math.round(x)}px`;
    gearTip.style.top = `${Math.round(above >= pad ? above : r.bottom + pad)}px`;
  };
  // Dust in/out of the trigger on the none↔visible edge only, never on a live re-render.
  const gearTipDustPoint = () => rectCenter(statusHost);
  const show = () => {
    const wasHidden = !gearTip.classList.contains('visible');
    renderGearTip();
    gearTip.classList.add('visible');
    placeGearTip();
    if (!wasHidden) return;
    surfaceIn(gearTip, gearTipDustPoint(), { ms: TIP_DUST_IN_MS });
  };
  const hide = () => {
    if (!gearTip.classList.contains('visible')) { settleSurface(gearTip); return; }
    gearTip.classList.remove('visible');
    surfaceOut(gearTip, gearTipDustPoint(), { ms: TIP_DUST_OUT_MS });
  };
  // A click focuses the trigger and opens its menu, so `focus` would re-show the tip
  // pointerdown just hid; suppressed for that first click only.
  let suppressFocusTip = false;
  statusHost.addEventListener('pointerenter', show);
  statusHost.addEventListener('focus', () => {
    if (suppressFocusTip) { suppressFocusTip = false; return; }
    show();
  });
  statusHost.addEventListener('pointerleave', hide);
  statusHost.addEventListener('blur', () => { suppressFocusTip = false; hide(); });
  statusHost.addEventListener('pointerdown', () => { suppressFocusTip = true; hide(); });
  return { show, hide, refresh: () => { if (gearTip.classList.contains('visible')) show(); } };
}

export function createChatStatusTip({ app, statusDot, statusHost }) {
  const tokenFor = (url) => serverBearerToken(app, url);
  let lastProbe = null;
  const tip = wireStatusTip(statusHost, () => lastProbe);
  const hideGearTip = tip.hide;

  const setDotState = (state, probe) => {
    statusDot.className = `conn-status conn-status-${state}`;
    lastProbe = probe;
    if (probe) cacheProbe(loadLlmSettings(), probe);
    tip.refresh();
  };
  // A refresh requested mid-probe queues and re-runs once, so the dot reflects the latest settings.
  let probing = false;
  let reprobe = false;
  const refreshStatus = async () => {
    if (probing) { reprobe = true; return; }
    probing = true;
    setDotState('connecting', null);
    try {
      const probe = await probeProvider(withSessionKey(loadLlmSettings()), { getToken: tokenFor });
      setDotState(probeStatusClass(probe), probe);
    } finally {
      probing = false;
      if (reprobe) { reprobe = false; refreshStatus(); }
    }
  };
  subscribe(EVENTS.llmSettingsChanged, () => refreshStatus());

  return { refreshStatus, hideGearTip };
}
