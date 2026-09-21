import { StencilElement, hostTag, define, wireModalShell } from '../base.js';
import { expirationModalInner } from './expirationMarkup.js';
import { notify } from '../../utils.js';
import { PERIOD_ORDER, DEFAULT_PERIOD, addPeriod } from '../../core/project/store/projectsStore.js';

// A local project's expiration: period + Refresh seed "now + period", a calendar picks any
// future day, keep-forever clears it, auto-refresh restarts the window on every open.
// Opened per row from the projects modal via openFor(id). Mirrors desktop's ExpirationDialog.
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
  #openFor;
  static inner() { return expirationModalInner(); }

  static template() {
    return hostTag('stencil-expiration-modal', 'id="expiration-modal-overlay" class="app-modal-overlay"', StencilExpirationModal.inner());
  }

// `anchors`: `from` the clicked menu row, `backTo` the "⋯" it hung off (the row is gone by
// the time the window closes).
  openFor(id, anchors) { this.#openFor?.(id, anchors); }

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

    let targetId = null;
    let keep = false;      // keep-forever (expiresAt === 0)
    let expiresAt = 0;     // working expiration, epoch ms
    let period = DEFAULT_PERIOD;
    let auto = true;
    let viewY = 0, viewM = 0; // calendar view month

    els.period.innerHTML = PERIOD_ORDER.map(p => `<option value="${p}">${PERIOD_LABELS[p]}</option>`).join('');

// The calendar floor is the current month (mirrors desktop's setMinimumDate(now)).
    const atFloor = () => {
      const t = new Date();
      return viewY < t.getFullYear() || (viewY === t.getFullYear() && viewM <= t.getMonth());
    };

    const setViewToExpiry = () => {
      const d = expiresAt ? new Date(expiresAt) : new Date();
      viewY = d.getFullYear();
      viewM = d.getMonth();
// An already-expired date is in the past; don't open below the floor.
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
      els.prev.disabled = atFloor();
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

// Stacked: opens over the projects list rather than replacing it.
    const { open, close } = wireModalShell(overlay, null, els.close, { stacked: true });
    this.#openFor = (id, { from = null, backTo = null } = {}) => { if (loadFrom(id)) { renderAll(); open(from, backTo); } };

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
        const ok = await app.confirm('Keep this project forever and remove its expiration date?', { title: 'Keep forever', confirmIcon: 'calendar' });
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

// Another tab may change this project's expiration while the dialog is open.
    app.tabs?.onProjectsChanged?.(() => {
      if (overlay.classList.contains('modal-open') && targetId != null && loadFrom(targetId)) renderAll();
    });
  }
}
define('stencil-expiration-modal', StencilExpirationModal);
