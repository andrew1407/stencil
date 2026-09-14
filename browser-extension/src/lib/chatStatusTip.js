// The assistant "…" trigger's status table (port of browser chatPanel.js gearStatusRows /
// showGearTip). It says only what the dropdown does not: reachability.
import {
  surfaceIn, surfaceOut, settleSurface, motionReduced, centerOf,
  TIP_DUST_IN_MS, TIP_DUST_OUT_MS,
} from './motion.js';

// `probe` null = in flight; `labels` maps a provider id to its display name.
export const gearStatusRows = (probe, labels = {}) => {
  if (!probe) return [{ label: 'Status', value: 'Checking the configured LLM…', state: 'connecting' }];
  if (probe.provider === 'none') {
    return [
      { label: 'Provider', value: labels.none },
      { label: 'Status', value: 'Assistant turned off — nothing is sent anywhere', state: 'error' },
    ];
  }
  const rows = [{ label: 'Provider', value: labels[probe.provider] || probe.provider || '—' }];
  if (probe.url) rows.push({ label: 'Endpoint', value: probe.url.replace(/^https?:\/\//i, '') });
  rows.push({ label: 'Model', value: probe.model || 'server default' });
  rows.push(probe.ok
    ? { label: 'Status', value: `Connected${probe.detail ? ` — ${probe.detail}` : ''}`, state: 'ok' }
    : { label: 'Status', value: probe.detail || 'Unreachable', state: 'error' });
  return rows;
};

export const gearTipFootText = (probe) => {
  const lines = [];
  if (!probe || probe.provider === 'none' || probe.ok) { /* the table says it all */ }
  else {
    lines.push(`No LLM reachable${probe.url ? ` at ${probe.url}` : ''} — ${probe.provider === 'ollama' ? 'start Ollama or ' : ''}configure another provider.`);
  }
  lines.push('Click to configure the assistant');
  return lines.join('\n');
};

// Dusts in/out of its anchor only on the none↔visible edge, never on a live re-render.
export const createChatStatusTip = ({ doc = globalThis.document, getAnchor = () => null,
                                      labels = {}, win = globalThis } = {}) => {
  let probe = null;   // null = probe in flight
  const tip = doc.createElement('div');
  tip.className = 'chat-status-tip';
  doc.body.appendChild(tip);
  const render = () => {
    tip.textContent = '';
    const table = doc.createElement('table');
    for (const r of gearStatusRows(probe, labels)) {
      const tr = doc.createElement('tr');
      const th = doc.createElement('th');
      th.textContent = r.label;
      const td = doc.createElement('td');
      td.textContent = r.value;
      if (r.state) td.classList.add(`chat-status-${r.state}`);
      tr.append(th, td);
      table.appendChild(tr);
    }
    const foot = doc.createElement('div');
    foot.className = 'chat-status-tip-foot';
    foot.textContent = gearTipFootText(probe);
    tip.append(table, foot);
  };
  const place = () => {
    const anchor = getAnchor();
    if (!anchor) return;
    const r = anchor.getBoundingClientRect();
    const t = tip.getBoundingClientRect();
    const pad = 8;
    const x = Math.max(pad, Math.min(r.left + r.width / 2 - t.width / 2, win.innerWidth - t.width - pad));
    const above = r.top - t.height - pad;
    tip.style.left = `${Math.round(x)}px`;
    tip.style.top = `${Math.round(above >= pad ? above : r.bottom + pad)}px`;
  };
  const show = () => {
    const wasHidden = !tip.classList.contains('visible');
    render();
    tip.classList.add('visible');
    place();
    if (!wasHidden) return;
    const point = centerOf(getAnchor());
    if (motionReduced() || !point || !surfaceIn(tip, point, { ms: TIP_DUST_IN_MS })) settleSurface(tip);
  };
  const hide = () => {
    if (!tip.classList.contains('visible')) { settleSurface(tip); return; }
    tip.classList.remove('visible');
    const point = centerOf(getAnchor());
    if (motionReduced() || !point || !surfaceOut(tip, point, { ms: TIP_DUST_OUT_MS })) settleSurface(tip);
  };
  const setProbe = (p) => {
    probe = p;
    if (tip.classList.contains('visible')) show();
  };
  // A click both focuses the trigger and opens the menu: the `focus` re-show that would
  // follow the `pointerdown` hide is suppressed for that one click.
  const wire = (btn) => {
    let suppressFocus = false;
    btn.addEventListener('pointerenter', show);
    btn.addEventListener('focus', () => {
      if (suppressFocus) { suppressFocus = false; return; }
      show();
    });
    btn.addEventListener('pointerleave', hide);
    btn.addEventListener('blur', () => { suppressFocus = false; hide(); });
    btn.addEventListener('pointerdown', () => { suppressFocus = true; hide(); });
  };
  return { el: tip, show, hide, setProbe, wire };
};
