// ── The §2.1 and §10 op executors ───────────────────────────────
// The ops that act on the SESSION rather than the pixels: turn attachments, project
// save/rename/remove, and the §10 editor settings. Same facade paths as the toolbar.
import { resolveServer } from './planValues.js';

export const SETTINGS_RUN = {
  // ── §2.1 multi-image ops: switch to a turn attachment / persist the result.
  // topLevelOnly shares the §10 enforcement (never inside variants or ask previews). ──
  image: async (a, { loadAttachment, notes }) => {
    if (!loadAttachment) throw new Error('This surface cannot switch to attached images');
    // §2.1: an index this turn cannot satisfy fails the ACTION, not the plan.
    try { await loadAttachment(a.index); }
    catch (err) { notes?.push(`Skipped switching to attached image ${a.index} — ${err?.message || err}`); }
  },
  save: async (a, { stencil, saveProject, notes }) => {
    if (!saveProject) throw new Error('This surface cannot save projects');
    // §2.1: saving nothing is a skipped action, never a failed plan.
    if (!stencil.imageSize) { notes?.push('Skipped save — no working image to save'); return; }
    // A destination cannot be honoured here — browser saves are local projects.
    if (a.path) notes?.push('Saved to the usual place — this surface cannot save to a path');
    // An incognito editor cannot hold a saved project — the save IS the request to leave it,
    // so promote (the desktop's chatSaveProject does the same) rather than refusing.
    if (stencil.incognito) {
      stencil.promoteIncognito?.();
      notes?.push('Left incognito — saved as a local project');
      return;
    }
    await saveProject(a.name || '');
  },

  // ── §10 editor-settings ops (browser & desktop editors only; forbidden in variants).
  // Same facade paths as the toolbar/settings UI: theme/accent use the flattened settings
  // accessors (not in apply()'s whitelist); the rest batch through stencil.apply. ──
  theme: (a, { stencil }) => { stencil.darkTheme = a.mode === 'dark'; },
  accent: (a, { stencil, notes }) => {
    if (a.color != null) { stencil.mainTheme = a.color; return; }
    // The facade routes a preset key to the persisting setAccent path and throws
    // on an unknown name — that throw is the §10 note, never a failed plan.
    try { stencil.mainTheme = a.preset; }
    catch (err) { notes?.push(`accent: ${err?.message || err}`); }
  },
  lineStyle: (a, { stencil }) => {
    const opts = {};
    if (a.color != null) opts.lineColor = a.color;
    if (a.pointColor != null) opts.pointColor = a.pointColor;
    if (a.thickness != null) opts.thickness = a.thickness;
    if (a.pointSize != null) opts.pointSize = a.pointSize;
    if (a.style != null) opts.lineStyle = a.style;
    if (a.drawMode != null) opts.drawMode = a.drawMode;
    if (a.fillColor != null) opts.fillColor = a.fillColor;
    stencil.apply(opts);
  },
  units: (a, { stencil }) => { stencil.apply({ unit: a.value }); },
  view: (a, { stencil }) => {
    const opts = {};
    if (a.points != null) opts.showPoints = a.points;
    if (a.lines != null) opts.showLines = a.lines;
    stencil.apply(opts);
  },
  clear: (_a, { stencil }) => { stencil.newEditor(); },
  openUrl: async (a, { stencil, userText, openIncognito, notes }) => {
    // The model may only ECHO the user: the exact URL must appear in the user's
    // own messages this conversation (the `connect` stance — a plan can never
    // introduce a host, and page/injected content can't smuggle one in).
    const typed = typeof userText === 'function' ? userText() : '';
    if (!typed.includes(a.url)) {
      throw new Error(`openUrl blocked: "${a.url}" is not a URL you gave in this conversation`);
    }
    if (a.incognito) {
      if (!openIncognito) throw new Error('This surface cannot open an incognito editor');
      // Adopts incognito in THIS editor and awaits the load, so the actions after it
      // act on the fetched picture exactly as the plain branch does.
      await openIncognito(a.url);
      notes?.push(`Loaded ${a.url} into a fresh incognito editor`);
      return;
    }
    try {
      await stencil.load(a.url);
    } catch (err) {
      // A bare TypeError("Failed to fetch") explains nothing — say what the editor CAN load.
      throw new Error(`Could not load ${a.url} (${err.message}) — the editor only loads direct `
        + 'image/video URLs from hosts that allow cross-origin reads; to pick images off a web '
        + "page, use the extension's assistant on that tab");
    }
    notes?.push(`Loaded ${a.url} into the editor`);
  },
  connect: async (a, { stencil, savedServers }) => {
    // Resolved entry = the SAVED { url, token } — auth is the stored connection's own.
    await stencil.connect(resolveServer(a.server, savedServers ? savedServers() : [], 'saved servers'));
  },
  disconnect: (a, { stencil }) => {
    stencil.disconnect(resolveServer(a.server, stencil.connections || [], 'connected servers'));
  },
  copy: async (a, { stencil, copyRendered, copyLayoutRendered, notes }) => {
    // §10 what:"layout": the layout JSON instead of the image — needs drawn lines.
    if (a.what === 'layout') {
      if (!(stencil.lines || []).length) { notes?.push('Skipped copy — no drawn lines to copy'); return; }
      try { await (copyLayoutRendered ? copyLayoutRendered() : stencil.copyLayout()); }
      catch (err) { notes?.push(`Copy to clipboard failed — ${err?.message || err}`); }
      return;
    }
    // §10: copying nothing is a skipped action, never a failed plan.
    if (!stencil.imageSize) { notes?.push('Skipped copy — no working image to copy'); return; }
    // The toolbar's DATA-section copy path. `copyRendered` (injected by the chat surface)
    // exposes the write's real outcome so a blocked clipboard lands in the REPLY as a
    // warning; without it, the chainable facade path fires as before.
    try { await (copyRendered ? copyRendered() : stencil.copyImage()); }
    catch (err) { notes?.push(`Copy to clipboard failed — ${err?.message || err}`); }
  },
  // §10 project management: both run the surface's EXISTING remove flows, incl. their
  // user confirmation — a declined confirm is a note, never a failed plan.
  removeProject: async (a, { stencil, removeProjectNamed, clearWorkingImage, notes }) => {
    if (!removeProjectNamed) throw new Error('This surface cannot manage projects');
    let name = a.name;
    if (a.current) {
      // The active SAVED project's name (an incognito or unsaved temporary
      // session is not a removable project).
      const cur = stencil.current;
      name = cur && !cur.incognito ? cur.name : null;
      if (!name) {
        // §10: nothing saved but a picture IS open — "remove this project" means the
        // thing on screen, so fall back to the `clear` flow behind the same confirm.
        if (clearWorkingImage && (stencil.imageSize || stencil.lines?.length)) {
          const cleared = await clearWorkingImage();
          if (cleared) notes?.push(`removeProject: ${cleared}`);
          return;
        }
        notes?.push('removeProject: no active saved project to remove');
        return;
      }
    }
    const note = await removeProjectNamed(name);
    if (note) notes?.push(`removeProject: ${note}`);
  },
  clearProjects: async (a, { clearLocalProjects, notes }) => {
    if (!clearLocalProjects) throw new Error('This surface cannot manage projects');
    const note = await clearLocalProjects(!!a.keepCurrent);
    if (note) notes?.push(`clearProjects: ${note}`);
  },
  // §10 compare: the original-vs-edit view control. View-only — the exported image
  // is unchanged, so this never counts as editing the picture.
  compare: (a, { stencil }) => {
    stencil.compareMode = a.mode;
    if (a.split != null) stencil.compareSplit = a.split;
  },
  // §10 zoom: the user's VIEW only — cropping is the crop op.
  zoom: (a, { stencil }) => {
    if (a.fit) stencil.zoomFit();
    else stencil.zoomLevel = a.percent;
  },
  // §10 renameProject: the inline project-rename control; the store's own errors
  // (duplicate name, no active project) come back as notes, never failed plans.
  renameProject: async (a, { renameActiveProject, notes }) => {
    if (!renameActiveProject) throw new Error('This surface cannot manage projects');
    const note = await renameActiveProject(a.name);
    if (note) notes?.push(`renameProject: ${note}`);
  },
  // §10 projectColor: the project name-colour control ("" restores the theme accent).
  projectColor: (a, { stencil, notes }) => {
    // The facade throws without an active project — a note, never a failed plan.
    try { stencil.projectColor = a.color; }
    catch (err) { notes?.push(`projectColor: ${err?.message || err}`); }
  },
  // §10 blankColor: recolour a BLANK project's background, keeping every drawn line.
  // Valid only on a blank project — note + skip otherwise.
  blankColor: async (a, { setBlankColor, notes }) => {
    if (!setBlankColor) throw new Error('This surface cannot recolour blank projects');
    const note = await setBlankColor(a.color);
    if (note) notes?.push(`blankColor: ${note}`);
  },
  // §10 openProject: the projects modal's open path — resolves like removeProject
  // (exact, else unique case-insensitive prefix); replacing an unsaved dirty editor
  // goes through the surface's own confirm inside the injected capability.
  openProject: async (a, { openProjectNamed, notes }) => {
    if (!openProjectNamed) throw new Error('This surface cannot manage projects');
    const note = await openProjectNamed(a.name || '', !!a.last);
    if (note) notes?.push(`openProject: ${note}`);
  },
  // §10 incognito: the toggle throws unless the editor is blank — note + skip.
  incognito: (a, { stencil, notes }) => {
    try { stencil.incognito = a.on; }
    catch (err) { notes?.push(`incognito: ${err?.message || err}`); }
  },
  // §10 voiceChat: browser-only hands-free voice chat toggle; the coordinator's
  // "not supported" throw becomes a note + skip via the capability's return.
  voiceChat: (a, { setVoiceChat, notes }) => {
    if (!setVoiceChat) throw new Error('This surface has no voice input');
    const note = setVoiceChat(a.on);
    if (note) notes?.push(`voiceChat: ${note}`);
  },
  // §10 chat: where the assistant panel itself sits. The one op that acts on the chat
  // window rather than the image — "open the chat on the right" is a thing users ask
  // for out loud, hands-free, with no hand on the mouse (user report).
  chatPanel: async (a, { setChatPlacement, notes }) => {
    if (!setChatPlacement) throw new Error('This surface has no assistant panel to place');
    const note = await setChatPlacement({ open: a.open, dock: a.dock });
    if (note) notes?.push(`chatPanel: ${note}`);
  },
  // §10 dialog: put one of the editor's own windows in front of the user — the answer to
  // "show me my projects" / "open the server list". Deferred like clearChat: the dialog
  // is MODAL, so it opens after the plan's other actions have run and the reply is on
  // screen, never in the middle of the turn.
  dialog: async (a, { openDialog, notes }) => {
    if (!openDialog) throw new Error('This surface has no dialogs to open');
    const note = await openDialog(a.close ? null : a.name);
    if (note) notes?.push(`dialog: ${note}`);
  },
  // §10 clearChat: the surface's clear-conversation flow behind the app's own confirm.
  // `deferred` runs it at the plan's END (a declined confirm costs nothing already done);
  // a caller `deferredSink` holds it past the §7 auto-continuation (chatController flushDeferred).
  clearChat: async (_a, { clearChatConversation, notes }) => {
    if (!clearChatConversation) throw new Error('This surface cannot clear the conversation');
    const note = await clearChatConversation();
    if (note) notes?.push(`clearChat: ${note}`);
  },
};
