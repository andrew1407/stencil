import { PROJECT_ACTION } from '../worker/messages.js';
import { buildOpenProjectUrl, buildExternalLaunchUrl } from './deepLink.js';
import { requireConnection } from '../net/remoteSync.js';
import * as meta from './projectMetaOps.js';
import * as xfer from './projectServerTransfer.js';

// ── ProjectTransferController: project lifecycle + local ↔ server transfer ──────
// Switching/opening projects, remove/clear, and the deps every part of the subsystem reads:
// the meta writes live in projectMetaOps.js, the move/copy flows in projectServerTransfer.js.
// Desktop twin: projectTransferController.cpp, whose Hooks pattern the `host` facade mirrors.
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

  // ── Meta writes (projectMetaOps.js) and local ↔ server transfer (projectServerTransfer.js).
  // Delegators, not re-exports: the projects modal, the console facade and the tests all
  // drive these through the controller instance that holds the deps.
  renameProject(id, name) { return meta.renameProject(this, id, name); }

  pushProjectFieldToServer(id, fields, failMsg) { return meta.pushProjectFieldToServer(this, id, fields, failMsg); }

  setProjectColor(id, color) { return meta.setProjectColor(this, id, color); }

  setProjectKeywords(id, keywords) { return meta.setProjectKeywords(this, id, keywords); }

  setProjectDescription(id, description) { return meta.setProjectDescription(this, id, description); }

  setProjectBlankColor(id, color) { return meta.setProjectBlankColor(this, id, color); }

  moveProjectToServer(id, address) { return xfer.moveProjectToServer(this, id, address); }

  copyProjectToServer(id, address, opts = {}) { return xfer.copyProjectToServer(this, id, address, opts); }

  moveProjectToLocal(m) { return xfer.moveProjectToLocal(this, m); }

  copyServerProjectToLocal(m, opts = {}) { return xfer.copyServerProjectToLocal(this, m, opts); }

  copyServerProjectToIncognito(m, opts = {}) { return xfer.copyServerProjectToIncognito(this, m, opts); }
}
