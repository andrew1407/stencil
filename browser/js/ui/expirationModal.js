import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';
import { PERIOD_ORDER, DEFAULT_PERIOD, addPeriod } from '../core/projectsStore.js';

// ── Component: project expiration modal ─────────────────────────
// Sets a LOCAL project's expiration date. A period selector + Refresh button
// seed "now + period"; a custom calendar grid lets the user pick any future day
// (today outlined one colour, the expiry day another; past days are disabled).
// A "keep forever" checkbox (confirmed) clears the date; an "auto-refresh on open"
// checkbox restarts the window every time the project is opened. Opened per-row
// from the projects modal via openFor(id). Mirrors desktop's ExpirationDialog.
const PERIOD_LABELS = {
  day: '1 day',
  week: '1 week',
  fortnight: '2 weeks (fortnight)',
  month: '1 month',
  '3month': '3 months',
  '6month': '6 months',
  year: '1 year',
};
const WEEKDAYS = ['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su'];
const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June',
  'July', 'August', 'September', 'October', 'November', 'December'];
const DAY_MS = 24 * 60 * 60 * 1000;

const startOfDay = (d) => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime();
const endOfDay = (d) => startOfDay(d) + DAY_MS - 1; // expires through the whole picked day
const sameDay = (a, b) => a.getFullYear() === b.getFullYear()
  && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
const fmtDay = (ts) => { try { return new Date(ts).toLocaleDateString(); } catch { return '—'; } };

export class StencilExpirationModal extends StencilElement {
  static inner() {
    return `
        <div class="app-modal exp-modal">
            <div class="settings-header">
                <h2>${icon('calendar', { size: 18 })} Project expiration</h2>
                <button class="app-modal-close btn-icon-text" id="expiration-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="exp-project" id="expiration-project"></div>
                <div class="vs-row exp-keep-row">
                    <label><input type="checkbox" id="expiration-keep"> Keep forever (never expires)</label>
                </div>
                <div class="vs-row" id="expiration-period-row">
                    <label for="expiration-period">Expires in</label>
                    <span class="exp-period-controls">
                        <select id="expiration-period"></select>
                        <button id="expiration-refresh" class="btn-icon-text" title="Set the expiration to now + the selected period">${icon('refresh', { size: 14 })}<span>Refresh</span></button>
                    </span>
                </div>
                <div class="vs-row" id="expiration-auto-row">
                    <label><input type="checkbox" id="expiration-auto"> Refresh expiration each time the project is opened</label>
                </div>
                <div class="exp-calendar" id="expiration-calendar">
                    <div class="exp-cal-head">
                        <button class="btn-icon" id="expiration-prev" title="Previous month">${icon('chevron-left', { size: 16 })}</button>
                        <span class="exp-cal-title" id="expiration-cal-title"></span>
                        <button class="btn-icon" id="expiration-next" title="Next month">${icon('chevron-right', { size: 16 })}</button>
                    </div>
                    <div class="exp-cal-grid" id="expiration-cal-grid"></div>
                </div>
                <div class="exp-legend">
                    <span class="exp-legend-item"><span class="exp-swatch exp-swatch-today"></span>Today: <b id="expiration-today"></b></span>
                    <span class="exp-legend-item"><span class="exp-swatch exp-swatch-expiry"></span>Expires: <b id="expiration-when"></b></span>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Past dates can’t be chosen. Expiration is local to this browser.</span>
                <button id="expiration-save" class="btn-icon-text">${icon('check')}<span>Save</span></button>
            </div>
        </div>
    `;
  }

  static template() {
    return hostTag('stencil-expiration-modal', 'id="expiration-modal-overlay" class="app-modal-overlay"', StencilExpirationModal.inner());
  }

  // Public entry point used by the projects modal row menu.
  openFor(id) { this._openFor?.(id); }

  wire(app) {
    const overlay = document.getElementById('expiration-modal-overlay');
    const el = (id) => document.getElementById(id);
    const els = {
      close: el('expiration-close'), project: el('expiration-project'),
      keep: el('expiration-keep'), periodRow: el('expiration-period-row'),
      period: el('expiration-period'), refresh: el('expiration-refresh'),
      auto: el('expiration-auto'), calendar: el('expiration-calendar'),
      calTitle: el('expiration-cal-title'), calGrid: el('expiration-cal-grid'),
      prev: el('expiration-prev'), next: el('expiration-next'),
      today: el('expiration-today'), when: el('expiration-when'),
      save: el('expiration-save'),
    };

    // Working state (committed on Save).
    let targetId = null;
    let keep = false;      // keep-forever (expiresAt === 0)
    let expiresAt = 0;     // working expiration, epoch ms
    let period = DEFAULT_PERIOD;
    let auto = true;
    let viewY = 0, viewM = 0; // calendar view month

    els.period.innerHTML = PERIOD_ORDER.map(p => `<option value="${p}">${PERIOD_LABELS[p]}</option>`).join('');

    // The calendar floor is the current month: every earlier day is disabled, so
    // navigating below it is pointless (mirrors desktop's setMinimumDate(now)).
    const atFloor = () => {
      const t = new Date();
      return viewY < t.getFullYear() || (viewY === t.getFullYear() && viewM <= t.getMonth());
    };

    const setViewToExpiry = () => {
      const d = expiresAt ? new Date(expiresAt) : new Date();
      viewY = d.getFullYear();
      viewM = d.getMonth();
      // An already-expired project's date is in the past; don't open below the floor.
      const t = new Date();
      if (viewY < t.getFullYear() || (viewY === t.getFullYear() && viewM < t.getMonth())) {
        viewY = t.getFullYear();
        viewM = t.getMonth();
      }
    };

    const renderControls = () => {
      els.keep.checked = keep;
      els.period.value = period;
      els.auto.checked = auto;
      els.period.disabled = keep;
      els.refresh.disabled = keep;
      els.periodRow.classList.toggle('is-disabled', keep);
      els.calendar.classList.toggle('is-disabled', keep);
      els.today.textContent = new Date().toLocaleDateString();
      els.when.textContent = keep ? 'never (kept forever)' : fmtDay(expiresAt);
    };

    const renderCalendar = () => {
      els.calTitle.textContent = `${MONTHS[viewM]} ${viewY}`;
      els.prev.disabled = atFloor(); // no navigating into fully-past months
      const grid = els.calGrid;
      grid.innerHTML = '';
      for (const w of WEEKDAYS) {
        const h = document.createElement('div');
        h.className = 'exp-cal-w';
        h.textContent = w;
        grid.appendChild(h);
      }
      const today = new Date();
      const todayStart = startOfDay(today);
      const first = new Date(viewY, viewM, 1);
      const lead = (first.getDay() + 6) % 7; // Monday-first
      for (let i = 0; i < lead; i++) {
        const e = document.createElement('div');
        e.className = 'exp-cal-cell exp-cal-empty';
        grid.appendChild(e);
      }
      const days = new Date(viewY, viewM + 1, 0).getDate();
      const expD = (!keep && expiresAt) ? new Date(expiresAt) : null;
      for (let d = 1; d <= days; d++) {
        const cellDate = new Date(viewY, viewM, d);
        const cell = document.createElement('button');
        cell.type = 'button';
        cell.className = 'exp-cal-cell';
        cell.textContent = String(d);
        const isPast = startOfDay(cellDate) < todayStart;
        if (isPast) { cell.classList.add('is-past'); cell.disabled = true; }
        if (sameDay(cellDate, today)) cell.classList.add('is-today');
        if (expD && sameDay(cellDate, expD)) cell.classList.add('is-expiry');
        if (!isPast && !keep) {
          cell.addEventListener('click', () => {
            expiresAt = endOfDay(cellDate);
            renderAll();
          });
        }
        grid.appendChild(cell);
      }
    };

    const renderAll = () => { renderControls(); renderCalendar(); };

    const loadFrom = (id) => {
      const meta = app.storage.store.getMeta(id);
      if (!meta) return false;
      targetId = id;
      els.project.textContent = meta.name || 'Untitled';
      period = meta.refreshPeriod || DEFAULT_PERIOD;
      auto = meta.autoRefresh !== false;
      expiresAt = meta.expiresAt || 0;
      keep = !expiresAt;
      setViewToExpiry();
      return true;
    };

    const { open, close } = wireModalShell(overlay, null, els.close);
    this._openFor = (id) => { if (loadFrom(id)) { renderAll(); open(); } };

    const seedFromPeriod = () => {
      keep = false;
      expiresAt = addPeriod(Date.now(), period);
      setViewToExpiry();
      renderAll();
    };

    els.period.addEventListener('change', () => { period = els.period.value; seedFromPeriod(); });
    els.refresh.addEventListener('click', seedFromPeriod);
    els.auto.addEventListener('change', () => { auto = els.auto.checked; });
    els.prev.addEventListener('click', () => {
      if (atFloor()) return;
      viewM--; if (viewM < 0) { viewM = 11; viewY--; }
      renderCalendar();
    });
    els.next.addEventListener('click', () => {
      viewM++; if (viewM > 11) { viewM = 0; viewY++; }
      renderCalendar();
    });
    els.keep.addEventListener('change', async () => {
      if (els.keep.checked) {
        const ok = await app.confirm('Keep this project forever and remove its expiration date?', { title: 'Keep forever' });
        if (!ok) { els.keep.checked = false; return; }
        keep = true;
        expiresAt = 0;
      } else {
        keep = false;
        expiresAt = addPeriod(Date.now(), period);
        setViewToExpiry();
      }
      renderAll();
    });
    els.save.addEventListener('click', () => {
      if (targetId == null) return;
      app.setProjectExpiration(targetId, {
        expiresAt: keep ? 0 : expiresAt,
        refreshPeriod: period,
        autoRefresh: auto,
      });
      close();
      notify(keep ? 'Project kept forever' : `Expires ${fmtDay(expiresAt)}`, 'ok');
    });

    // Live cross-tab: if another tab changes this project's expiration while the
    // dialog is open, reflect the authoritative stored value.
    app.tabs?.onProjectsChanged?.(() => {
      if (overlay.classList.contains('modal-open') && targetId != null && loadFrom(targetId)) renderAll();
    });
  }
}
define('stencil-expiration-modal', StencilExpirationModal);
