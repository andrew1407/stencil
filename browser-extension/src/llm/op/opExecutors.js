// ── One executor per §8 op ──────────────────────────────────────────────────
// Keyed like the validators (opPlan.js EXT_VALIDATORS). Each mutates the shared
// execution context `x` ({ listing, cards, warnings, attached, attachedIndices }).
// Every capability is injected; a surface that lacks one warns instead of throwing.
// `history` is the controller's live message array — openUrl's echo guard reads it.

export const createOpExecutors = ({
  history = [], focusImage, openImage, attachImage, pinImage, unpinImage, openUrlImage,
  setTheme, setAccent, setFilters, scanTab, rescan, askClear = () => {},
} = {}) => ({
  focus: async (a, x) => {
    const entry = x.listing[a.image];
    let ok = false;
    try { ok = !!(await focusImage(a.image, entry)); } catch { ok = false; }
    x.cards.push({ kind: 'focus', index: a.image, entry, ok });
  },
  open: async (a, x) => {
    const entry = x.listing[a.image];
    try {
      const extra = await openImage(a, entry);
      if (Array.isArray(extra)) x.warnings.push(...extra);
      x.cards.push({ kind: 'open', index: a.image, entry, ok: true });
    } catch (err) {
      x.warnings.push(`Could not open image ${a.image}: ${err.message}`);
      x.cards.push({ kind: 'open', index: a.image, entry, ok: false });
    }
  },
  attach: async (a, x) => {
    const got = [];   // this action's successes only — the card must not repeat earlier actions'
    for (const idx of a.indices) {
      try {
        const img = await attachImage(idx, x.listing[idx]);
        if (img) { x.attached.push(img); got.push(idx); }
      } catch (err) {
        x.warnings.push(`Could not attach image ${idx}: ${err.message}`);
      }
    }
    x.attachedIndices.push(...got);
    x.cards.push({ kind: 'attach', indices: a.indices.slice(), attached: got });
  },
  pin: async (a, x) => {
    const got = [];
    for (const idx of a.indices) {
      try {
        await pinImage(idx, x.listing[idx]);
        got.push(idx);
      } catch (err) {
        x.warnings.push(`Could not pin image ${idx}: ${err.message}`);
      }
    }
    x.cards.push({ kind: 'pin', indices: a.indices.slice(), pinned: got });
  },
  // The other half of pin (§8): same per-index execution, local pins only.
  unpin: async (a, x) => {
    const got = [];
    for (const idx of a.indices) {
      try {
        await unpinImage(idx, x.listing[idx]);
        got.push(idx);
      } catch (err) {
        x.warnings.push(`Could not unpin image ${idx}: ${err.message}`);
      }
    }
    x.cards.push({ kind: 'unpin', indices: a.indices.slice(), unpinned: got });
  },
  openUrl: async (a, x) => {
    // The model may only ECHO the user: the exact URL must appear in the user's own messages this
    // conversation — page content, scan results and the model never introduce a host (§10 parity).
    const typed = history.filter((m) => m.role === 'user').map((m) => m.text).join('\n');
    if (!typed.includes(a.url)) {
      x.warnings.push(`openUrl blocked: "${a.url}" is not a URL you gave in this conversation`);
      x.cards.push({ kind: 'openUrl', url: a.url, ok: false });
      return;
    }
    try {
      await openUrlImage(a);
      x.cards.push({ kind: 'openUrl', url: a.url, incognito: !!a.incognito, ok: true });
    } catch (err) {
      x.warnings.push(`Could not open ${a.url}: ${err.message}`);
      x.cards.push({ kind: 'openUrl', url: a.url, incognito: !!a.incognito, ok: false });
    }
  },
  // ── Panel settings: the assistant driving the surface's own controls. They touch nothing
  // on the page, so they carry no listing indices and never gather context.
  theme: async (a, x) => {
    if (!setTheme) { x.warnings.push('Changing the theme is not supported here'); return; }
    try {
      await setTheme(a.mode);
      x.cards.push({ kind: 'theme', mode: a.mode, ok: true });
    } catch (err) {
      x.warnings.push(`Could not switch the theme: ${err.message}`);
      x.cards.push({ kind: 'theme', mode: a.mode, ok: false });
    }
  },
  accent: async (a, x) => {
    if (!setAccent) { x.warnings.push('Changing the accent is not supported here'); return; }
    const asked = a.color || a.preset;
    try {
      const res = (await setAccent(a)) || {};
      x.cards.push({ kind: 'accent', asked, applied: res.label || asked, exact: res.exact !== false, ok: true });
    } catch (err) {
      x.warnings.push(`Could not change the accent: ${err.message}`);
      x.cards.push({ kind: 'accent', asked, ok: false });
    }
  },
  filter: async (a, x) => {
    if (!setFilters) { x.warnings.push('Changing the filters is not supported here'); return; }
    try {
      const res = await setFilters(a);
      x.cards.push({ kind: 'filter', applied: (res && res.applied) || [], ok: true });
    } catch (err) {
      x.warnings.push(`Could not change the filters: ${err.message}`);
      x.cards.push({ kind: 'filter', applied: [], ok: false });
    }
  },
  scanTab: async (a, x) => {
    const tab = x.tabs[a.tab];
    let res = null;
    try { res = await scanTab(tab, a.tab); } catch (err) { res = { ok: false, error: err.message }; }
    if (res && res.ok) {
      // The owner swapped the working listing; the continuation round re-reads it.
      x.scannedTab = { index: a.tab, title: res.title || (tab && tab.title) || '', count: res.count || 0 };
      x.cards.push({ kind: 'scanTab', index: a.tab, title: x.scannedTab.title, count: x.scannedTab.count, ok: true });
    } else {
      x.warnings.push(`Could not scan tab ${a.tab}: ${(res && res.error) || 'the scan failed'}`);
      x.cards.push({ kind: 'scanTab', index: a.tab, title: (tab && tab.title) || '', ok: false });
    }
  },
  // §10 clearChat: never executed in plan order — only marked. send() shows the
  // confirm after the turn's other actions and any continuation round complete.
  clearChat: async () => { askClear(); },
  // Re-scan the CURRENT page (§8): the working listing refreshes in place; a GATHER
  // op, so a rescan-only plan re-sends the turn once over the fresh listing.
  rescan: async (a, x) => {
    if (!rescan) { x.warnings.push('Re-scanning is not supported here'); return; }
    let res = null;
    try { res = await rescan(); } catch (err) { res = { ok: false, error: err.message }; }
    if (res && res.ok) {
      x.rescanned = { count: res.count || 0 };
      x.cards.push({ kind: 'rescan', count: x.rescanned.count, ok: true });
    } else {
      x.warnings.push(`Could not re-scan the page: ${(res && res.error) || 'the scan failed'}`);
      x.cards.push({ kind: 'rescan', ok: false });
    }
  },
});
