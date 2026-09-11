import { notify } from '../utils.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import { normalizeHex } from './accents.js';
import { buildLayoutPayload, normalizeCropRect } from './layout.js';
import { buildOpenProjectUrl, buildExternalLaunchUrl } from './deepLink.js';
import { requireConnection, createRemoteProject, saveRemoteProject } from '../net/remoteSync.js';
import { getSyncToServer } from '../net/connectionStore.js';

// ── ProjectTransferController: project lifecycle + local ↔ server transfer ──────
// Switching/opening projects, the meta writes with their server pushes, remove/clear, and
// the move/copy flows between local storage and a collaboration server. Desktop twin:
// projectTransferController.cpp, whose Hooks pattern the `host` facade mirrors.
//
// Explicit deps, so the subsystem unit-tests without a DrawingApp:
//   storage        — Storage (registry, save/loadProject/newTemporary, temp/incognito flags)
//   tabs           — TabsCoordinator (peer-tab broadcasts)
//   remoteSync     — RemoteSyncController (reloadRemoteActive, fetchRemoteOriginal)
//   getConnections — () => ConnectionManager; a getter, since stencilApi creates it lazily
//   host           — the narrow app facade: activeProjectId + remoteLink (get/set),
//                    blankColor/imageBaseName (set), chatPersistence (get), and the session
//                    callbacks updateProjectTitle / updateIncognitoUI / newEditor /
//                    loadImageFromFile / setBlankColor.
export class ProjectTransferController {
  constructor({ storage, tabs, remoteSync, getConnections, host }) {
    this.storage = storage;
    this.tabs = tabs;
    this.remoteSync = remoteSync;
    this.getConnections = getConnections;
    this.host = host;
  }

  // ── Multi-project navigation (called by the projects modal) ──────
  // Switch the editor to a saved project, persisting the current one first.
  switchToProject(id) {
    const host = this.host;
    if (id === host.activeProjectId) return false;
    if (!this.storage.temporary && host.activeProjectId != null) this.storage.save();
    if (this.storage.loadProject(id)) {
      host.activeProjectId = id;
      // Restore the remote link from meta so a reopened server-backed project keeps its
      // identity (outline + write-back); purely-local projects clear it.
      const meta = this.storage.store.getMeta(id);
      host.remoteLink = (meta && meta.remoteId && meta.address)
        ? { address: meta.address, remoteId: meta.remoteId, version: meta.remoteVersion || 0 }
        : null;
      // Restore the blank-fill colour so the blank-colour control reappears for a reopened blank.
      host.blankColor = (meta && meta.blank && meta.blankColor) ? meta.blankColor : '';
      this.tabs.reportActive(id);
      host.updateProjectTitle();   // reflect (or clear) the remote badge + outline now
      // Server-linked: pull the latest so a reopen shows peers' newest state, not stale cache.
      if (host.remoteLink && getSyncToServer()) this.remoteSync.reloadRemoteActive();
      // Chat persistence (§12): with saving on, swap in this project's saved chat.
      host.chatPersistence?.projectOpened(id);
      return true;
    }
    return false;
  }

  // Open a saved project in a NEW browser tab, leaving this tab untouched. The
  // new tab boots with a "?open=<id>" deep link that applyProjectDeepLink()
  // consumes. Default open-in-current-tab behavior stays on switchToProject().
  openProjectInNewTab(id, win = null) {
    if (id == null) { if (win) win.close(); return; }
    const base = location.origin + location.pathname;
    const url = buildOpenProjectUrl(base, id);
    // `win` is a tab the caller pre-opened synchronously (inside the user gesture) so a strict
    // popup blocker can't swallow it after an async confirm; navigate it instead of opening anew.
    if (win) win.location = url; else window.open(url, '_blank');
  }

  // Open a SERVER project in a new tab via the server-launch fragment (consumed by
  // applyExternalLaunch on the new tab). Mirrors openProjectInNewTab for local ids.
  openRemoteProjectInNewTab(meta, win = null) {
    if (!meta || !meta.serverUrl || meta.id == null) { if (win) win.close(); return; }
    const base = location.origin + location.pathname;
    const url = buildExternalLaunchUrl(base, { server: { url: meta.serverUrl, id: meta.id, version: meta.version || 0 } });
    if (win) win.location = url; else window.open(url, '_blank');
  }

  // Fetch a remote project's image + layout and load it into the editor, linking the
  // session for live co-edit. If a local project is already linked to this server
  // project, just switch to it — never create a duplicate local copy or re-download.
  // `meta` needs { serverUrl, id }; name/source enrich the fallback filename.
  async openRemoteProject(meta) {
    const linked = this.storage.store.list().find(m => m.remoteId === meta.id && m.address === meta.serverUrl);
    if (linked) { this.switchToProject(linked.id); return; }
    const conn = requireConnection(this.getConnections(), meta.serverUrl);
    const full = await conn.getProject(meta.id);
    // Prefer the server's stored original bytes; if it holds none, fetch the `source`
    // URL directly (cross-origin, so it needs CORS — which typical image hosts send).
    const src = full.project?.source || meta.source || '';
    const blob = await this.remoteSync.fetchRemoteOriginal(conn, meta.id, src);
    if (!blob) throw new Error('no image bytes on the server');
    const ext = (blob.type && blob.type.split('/')[1]) || 'png';
    const name = full.project?.name || meta.name || 'image';
    const file = new File([blob], `${name}.${ext}`, { type: blob.type || 'image/png' });
    this.host.loadImageFromFile(file, {
      source: src,
      resource: full.project?.resource || '',
      color: full.project?.color || '',
      address: meta.serverUrl,
      remoteId: meta.id,
      version: full.project?.version || 0,
      layout: full.layout,
    });
  }

  // Prolong a project: reset its 7-day expiry window to start from now. Notifies
  // peers so their open project lists re-render with the new expiry.
  renewProject(id) {
    const meta = this.storage.store.renew(id);
    if (meta) this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    return meta;
  }

  // Set a project's expiration fields exactly (from the expiration modal / facade).
  // opts: { expiresAt (0 = keep forever), refreshPeriod, autoRefresh }. Broadcasts so
  // the projects list + any open expiration dialog in other tabs re-render.
  setProjectExpiration(id, opts = {}) {
    const meta = this.storage.store.setExpiration(id, opts);
    if (meta) {
      this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
      // An explicit expiry change (not a refreshPeriod/autoRefresh-only tweak, which are
      // local-only concepts) propagates to the collaboration server for a server-linked
      // project — best-effort, like setProjectColor. Server projects otherwise have no
      // expiry unless one is set here explicitly.
      if (Object.prototype.hasOwnProperty.call(opts, 'expiresAt')) {
        this.pushProjectFieldToServer(id, { expiresAt: meta.expiresAt || 0 }, 'Could not set expiration on the server');
      }
    }
    return meta;
  }

  // Close a project's editor (without deleting the saved project). Active in THIS tab →
  // blank editor; open in ANOTHER tab → ask it to via a CLOSE broadcast. `fully` also closes
  // this tab/window (best-effort — only script-opened windows can self-close).
  closeProject(id, { fully = false } = {}) {
    if (id != null && id === this.host.activeProjectId) this.host.newEditor();
    else if (id != null) this.tabs.projectsChanged({ id, action: PROJECT_ACTION.CLOSE });
    if (fully) { try { window.close(); } catch { /* not closeable */ } }
  }

  // Rename a project. Registry meta is the source of truth for the projects list, and
  // save()'s name fallback prefers it over imageBaseName, so an active-project rename
  // survives saves. Notifies peers to re-render. Returns updated meta (null for unknown id).
  renameProject(id, name) {
    const clean = String(name || '').trim();
    if (!clean) return null;
    // Names must be unique across projects. The UI surfaces null as "kept old name";
    // the console's Project.name setter checks store.nameExists() first to throw.
    if (this.storage.store.nameExists(clean, id)) {
      notify(`A project named “${clean}” already exists`, 'fail');
      return null;
    }
    const meta = this.storage.store.rename(id, clean);
    if (meta) {
      // The project name is THE name: keep the working/download name (imageBaseName)
      // in lockstep for the active project, no matter which surface renamed it
      // (topbar, projects list, links modal, console). No separate image name to track.
      if (id === this.host.activeProjectId) {
        this.host.imageBaseName = clean;
        this.host.updateProjectTitle();   // refresh tab title + topbar field
      }
      this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
      // Push the rename to the server immediately (like setProjectColor), so peers see it live.
      this.pushProjectFieldToServer(id, { name: clean }, 'Could not rename the project on the server');
    }
    return meta;
  }

  // Push a single field change (rename / colour) to the collaboration server for a
  // server-linked project. The active project uses its live remoteLink (and adopts the bumped
  // version); a non-active linked project uses its stored meta. Version-guarded + best-effort
  // (no-op when not linked / sync off) — a failure only notifies with `failMsg`.
  async pushProjectFieldToServer(id, fields, failMsg) {
    if (!getSyncToServer()) return;
    const host = this.host;
    const active = id === host.activeProjectId && !!host.remoteLink;
    const meta = this.storage.store.getMeta(id) || {};
    const address = active ? host.remoteLink.address : meta.address;
    const remoteId = active ? host.remoteLink.remoteId : meta.remoteId;
    if (!address || !remoteId) return;
    let conn;
    try {
      conn = requireConnection(this.getConnections(), address);
    } catch (err) {
      notify(err.message, 'fail');
      return;
    }
    // Version-guarded write with a bounded conflict retry (mirrors the CLI's
    // putProjectField). A stale cached version — a concurrent field push / layout
    // save from THIS client racing on remoteLink.version, or a peer's edit — 409s;
    // re-read the server's current version and retry so the change isn't silently
    // lost. Single-field sets are idempotent, so last-writer-wins is correct here.
    let version = active ? host.remoteLink.version : (meta.remoteVersion || 0);
    for (let attempt = 0; attempt < 4; attempt++) {
      try {
        const rec = await conn.updateProject(remoteId, { ...fields, version });
        // Adopt the bumped version only if remoteLink still points at this same
        // project (the user may have switched projects during the await).
        if (rec && rec.version != null && host.activeProjectId === id
            && host.remoteLink && host.remoteLink.remoteId === remoteId) {
          host.remoteLink = { ...host.remoteLink, version: rec.version };
        }
        return;
      } catch (err) {
        if (err && err.status === 409 && attempt < 3) {
          version = await this.#currentRemoteVersion(conn, remoteId, version);
          continue;
        }
        notify(`${failMsg} — ${err.message}`, 'fail');
        return;
      }
    }
  }

  // Re-read a linked project's current server version (after a 409 or a file write
  // that bumps it without returning it), falling back to `fallback` on any error.
  async #currentRemoteVersion(conn, remoteId, fallback) {
    try {
      const full = await conn.getProject(remoteId);
      const v = full && full.project ? full.project.version : undefined;
      return v == null ? fallback : v;
    } catch {
      return fallback;
    }
  }

  // Set (or clear) a project's accent colour — the custom colour its NAME is painted in
  // wherever it appears. An empty/whitespace `color` clears it (back to the theme accent);
  // a valid hex is normalised to "#rrggbb". Invalid hex is rejected (keeps the old colour).
  // Persists to the registry, repaints the active-project UI, notifies peers, and pushes the
  // colour to the server for a server-linked project. Returns updated meta (null for unknown id).
  setProjectColor(id, color) {
    const raw = String(color == null ? '' : color).trim();
    let next = '';   // empty → explicit clear (theme fallback)
    if (raw) {
      next = normalizeHex(raw);
      if (!next) {
        notify(`“${color}” is not a valid hex color`, 'fail');
        return null;
      }
    }
    const meta = this.storage.store.setColor(id, next);
    if (!meta) return null;
    if (id === this.host.activeProjectId) this.host.updateProjectTitle();
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    // Best-effort server push for a server-linked project (no-op when not linked).
    this.pushProjectFieldToServer(id, { color: next }, 'Could not set project color on the server');
    return meta;
  }

  // Set a project's search keywords (normalized by the store). Mirrors setProjectColor:
  // writes local meta, broadcasts to peer tabs, and best-effort pushes to the server for a
  // server-linked project. Returns the stored meta, or null on unknown id.
  setProjectKeywords(id, keywords) {
    const meta = this.storage.store.setKeywords(id, keywords);
    if (!meta) return null;
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    this.pushProjectFieldToServer(id, { keywords: meta.keywords }, 'Could not set project keywords on the server');
    return meta;
  }

  // Set a project's free-text description (trimmed by the store; '' clears). Same shape as
  // setProjectKeywords: local meta, peer tabs, best-effort server push. Null on unknown id.
  setProjectDescription(id, description) {
    const meta = this.storage.store.setDescription(id, description);
    if (!meta) return null;
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    this.pushProjectFieldToServer(id, { description: meta.description }, 'Could not set project description on the server');
    return meta;
  }

  // Set a project's blank-fill colour by id. No-op (null) for a non-blank project (only blanks have
  // a blank colour). When `id` is the ACTIVE project, recolours the visible background in place
  // (setBlankColor); otherwise updates the stored meta + peers + server. `color` is any normalizeHex
  // form. Returns the stored meta, or null.
  setProjectBlankColor(id, color) {
    const cur = this.storage.store.getMeta(id);
    if (!cur || !cur.blank) return null;
    const next = normalizeHex(color);
    if (!next) return null;
    if (id === this.host.activeProjectId) { this.host.setBlankColor(next); return this.storage.store.getMeta(id); }
    const meta = this.storage.store.setBlankColor(id, next);
    if (!meta) return null;
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    this.pushProjectFieldToServer(id, { blankColor: meta.blankColor }, 'Could not set blank color on the server');
    return meta;
  }

  // Remove one project; if it's the active one, drop to a blank editor.
  removeProject(id) {
    if (id === this.host.activeProjectId) {
      this.storage.store.remove(id);
      this.storage.newTemporary();
      this.tabs.reportActive(null);
    } else {
      this.storage.store.remove(id);
    }
    this.host.chatPersistence?.projectRemoved(id);   // its stored chat goes with it (§12.2)
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.REMOVED });
  }

  // Permanently delete every saved project, then drop to a blank editor.
  clearAllProjects() {
    this.storage.store.clearAll();
    this.host.chatPersistence?.allProjectsCleared();   // stored chats go with their projects (§12.2)
    this.storage.newTemporary();
    this.tabs.reportActive(null);
    this.tabs.projectsChanged({ action: PROJECT_ACTION.CLEARED });
  }

  // ── Move / copy a project between local storage and a server ──────
  // Create a NEW server project from a local project's content (original bytes + annotated
  // layout) under `name`. Shared by move (then links the local) and copy (leaves local as-is).
  // Returns { link, proj, meta }. Flushes the active project first so the server gets latest.
  async #createServerFromLocal(id, address, name = null) {
    const conn = requireConnection(this.getConnections(), address);
    if (id === this.host.activeProjectId && !this.storage.temporary) this.storage.save();   // flush latest
    const proj = this.storage.store.get(id);
    if (!proj) throw new Error('Project not found');
    const meta = this.storage.store.getMeta(id) || {};
    const payload = proj.payload || {};
    const layout = payload.layout || {};
    // Decode the stored original (a data URL) to raw bytes for the codec-free server.
    let bytes = null;
    let ext = meta.imageExt || layout.imageExt || 'png';
    const w = layout.imageWidth || meta.imageW || 0;
    const h = layout.imageHeight || meta.imageH || 0;
    if (payload.image) {
      const blob = await (await fetch(payload.image)).blob();
      bytes = new Uint8Array(await blob.arrayBuffer());
      if (blob.type && blob.type.includes('/')) ext = blob.type.split('/')[1];
    }
    const projName = (name && name.trim()) || meta.name || layout.imageBaseName || 'Untitled';
    const link = await createRemoteProject(conn, {
      name: projName,
      source: meta.source || layout.imageSource || '',
      resource: meta.resource || layout.imageResource || '',
      color: meta.color || '',
      bytes, ext, w, h,
    });
    // Push the annotated layout (lines + filter) so the server holds the full project.
    // The layout save bumps the server version again, so adopt the refreshed link it
    // returns — otherwise `link.version` stays at the create-time value and the next
    // version-guarded field push (colour / rename / expiry) 409s against the server.
    const savedLink = await saveRemoteProject(conn, link, {
      name: projName,
      layout: buildLayoutPayload({
        imageWidth: w, imageHeight: h,
        lines: layout.lines || [],
        imageFilter: layout.imageFilter,
        filterColor: layout.filterColor,
        cropRect: layout.cropRect,
        rotationQuarters: layout.rotationQuarters,
        pageSize: layout.pageSize,
        customPageWidth: layout.customPageWidth,
        customPageHeight: layout.customPageHeight,
        allowFormulas: layout.allowFormulas,
        formulaX: layout.formulaX,
        formulaY: layout.formulaY,
      }),
    });
    return { link: savedLink, proj, meta };
  }

  // Local → server: create the project on `address`, then LINK the local copy to it (keeping
  // the editor open + the row in place). Returns the new remote id.
  async moveProjectToServer(id, address) {
    const { link, proj, meta } = await this.#createServerFromLocal(id, address);
    const linkedMeta = { ...meta, id, address: link.address, remoteId: link.remoteId, remoteVersion: link.version };
    this.storage.store.upsert(linkedMeta, proj.payload || {});
    if (id === this.host.activeProjectId) {
      this.host.remoteLink = { address: link.address, remoteId: link.remoteId, version: link.version };
      this.host.updateProjectTitle();   // reflect the golden remote outline now
    }
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    return link.remoteId;
  }

  // Local → server COPY: create a new server project from the local one (default name
  // "<name>-copy") and LEAVE the local project untouched. Returns the new remote id.
  async copyProjectToServer(id, address, { name } = {}) {
    const base = this.storage.store.getMeta(id)?.name || 'Untitled';
    const copyName = (name && name.trim()) || `${base}-copy`;
    const { link } = await this.#createServerFromLocal(id, address, copyName);
    this.tabs.projectsChanged({ action: PROJECT_ACTION.UPDATED });   // refresh the remote rows
    return link.remoteId;
  }

  // Server → local: fetch the server project's image + layout, save it as a new
  // local project, then delete it from the server. `meta` is a remote-project meta
  // ({ id, serverUrl, name, source }). Returns the new local project id.
  async moveProjectToLocal(meta) {
    const host = this.host;
    // If the moved server project is the open session (or its local cache), follow it to the
    // new local id so the editor stays open + focused instead of pointing at a deleted server id.
    const openCacheId = (host.remoteLink && host.remoteLink.remoteId === meta.id
      && host.remoteLink.address === meta.serverUrl) ? host.activeProjectId : null;
    const newId = await this.#importServerProjectToLocal(meta, { removeFromServer: true });
    if (openCacheId != null) {
      if (openCacheId !== newId) this.storage.store.remove(openCacheId);   // drop the now-stale cache
      this.switchToProject(newId);
    }
    return newId;
  }

  // Make a detached LOCAL copy of a server project, leaving the server copy in place. Default
  // name "<name>-copy" (override via `name`). Returns the new local project id; caller opens it.
  async copyServerProjectToLocal(meta, { name } = {}) {
    return this.#importServerProjectToLocal(meta, { removeFromServer: false, copy: true, name });
  }

  // Copy a server project into an INCOGNITO session (no local record, no server link). Current
  // tab: replace the editor with the image + annotations as incognito. New tab: hand off the
  // image via the external-launch URL (image only — the launch payload carries no annotations).
  async copyServerProjectToIncognito(meta, { newTab = false } = {}) {
    const conn = requireConnection(this.getConnections(), meta.serverUrl);
    const full = await conn.getProject(meta.id);
    const src = full.project?.source || meta.source || '';
    const blob = await this.remoteSync.fetchRemoteOriginal(conn, meta.id, src);
    if (!blob) throw new Error('no image bytes on the server');
    const ext = (blob.type && blob.type.split('/')[1]) || 'png';
    const name = full.project?.name || meta.name || 'Untitled';
    if (newTab) {
      const dataUrl = await this.#blobToDataUrl(blob);
      const url = buildExternalLaunchUrl(location.origin + location.pathname, { dataUrl, name, incognito: true });
      window.open(url, '_blank');
      return;
    }
    const file = new File([blob], `${name}.${ext}`, { type: blob.type || 'image/png' });
    if (!this.storage.incognito) this.storage.save();   // flush any current project first
    this.host.newEditor();
    this.storage.incognito = true;
    this.host.updateIncognitoUI();
    // adoptLayout applies the lines/filter/crop/page/formulas without linking (no remoteId).
    this.host.loadImageFromFile(file, { source: src, resource: full.project?.resource || '', layout: full.layout, adoptLayout: true });
  }

  // Read a Blob into a data URL (used by the new-tab incognito hand-off).
  #blobToDataUrl(blob) {
    return new Promise((res, rej) => {
      const r = new FileReader();
      r.onload = () => res(r.result);
      r.onerror = () => rej(new Error('could not read image bytes'));
      r.readAsDataURL(blob);
    });
  }

  // Shared body of move/copy server→local: fetch image + layout, persist a fresh detached
  // local project (crop/rotation included), optionally delete the server copy. `copy` defaults
  // the name to "<base>-copy"; an explicit `name` overrides.
  async #importServerProjectToLocal(meta, { removeFromServer = false, copy = false, name = null } = {}) {
    const conn = requireConnection(this.getConnections(), meta.serverUrl);
    const full = await conn.getProject(meta.id);
    const src = full.project?.source || meta.source || '';
    const blob = await this.remoteSync.fetchRemoteOriginal(conn, meta.id, src);
    const dataUrl = blob ? await this.#blobToDataUrl(blob) : null;
    const sl = full.layout || {};
    const newId = this.storage.store.createId();
    const base = full.project?.name || meta.name || 'Untitled';
    const projName = (name && name.trim()) || (copy ? `${base}-copy` : base);
    const localMeta = {
      id: newId,
      name: projName,
      color: full.project?.color || '',
      thumbnail: dataUrl,
      createdAt: Date.now(),
      hasImage: !!dataUrl,
      imageW: sl.imageWidth || 0,
      imageH: sl.imageHeight || 0,
      source: src || null,
      resource: full.project?.resource || null,
      address: null,
      remoteId: null,
      remoteVersion: 0,
    };
    this.storage.store.upsert(localMeta, {
      image: dataUrl,
      layout: {
        imageWidth: sl.imageWidth || 0,
        imageHeight: sl.imageHeight || 0,
        lines: Array.isArray(sl.lines) ? sl.lines : [],
        imageFilter: sl.imageFilter || 'none',
        filterColor: sl.filterColor || '#7c3aed',
        cropRect: normalizeCropRect(sl.cropRect),   // server rects are canonical {w,h}; store internal shape
        rotationQuarters: sl.rotationQuarters || 0,
        // Carry page format + formulas so the detached local copy keeps them.
        pageSize: sl.pageSize || 'A3',
        customPageWidth: sl.customPageWidth || 21,
        customPageHeight: sl.customPageHeight || 29.7,
        allowFormulas: !!sl.allowFormulas,
        formulaX: sl.formulaX || '',
        formulaY: sl.formulaY || '',
        imageBaseName: projName,
        imageExt: (blob && blob.type && blob.type.includes('/')) ? blob.type.split('/')[1] : 'png',
        imageSource: src || null,
        imageResource: full.project?.resource || null,
      },
    });
    // Remove from the server only for a move (the live feed re-renders its golden row out).
    if (removeFromServer) await conn.deleteProject(meta.id);
    this.tabs.projectsChanged({ id: newId, action: PROJECT_ACTION.UPDATED });
    return newId;
  }
}
