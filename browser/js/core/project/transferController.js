import { PROJECT_ACTION } from '../../worker/messages.js';
import { buildOpenProjectUrl, buildExternalLaunchUrl } from '../launch/deepLink.js';
import { requireConnection } from '../../net/remoteSync.js';
import * as meta from './meta/projectMetaOps.js';
import * as xfer from './serverTransfer.js';

// Project lifecycle + local ↔ server transfer; the meta writes live in projectMetaOps.js,
// the move/copy flows in serverTransfer.js.
// Desktop twin: ProjectTransferController.cpp, whose Hooks pattern the `host` facade mirrors.
// Deps are explicit so it unit-tests without a DrawingApp: storage, tabs, remoteSync,
// getConnections (a getter — stencilApi creates it lazily) and host.
export class ProjectTransferController {
  constructor({ storage, tabs, remoteSync, getConnections, host }) {
    this.storage = storage;
    this.tabs = tabs;
    this.remoteSync = remoteSync;
    this.getConnections = getConnections;
    this.host = host;
  }

  // Switch the editor to a saved project, persisting the current one first.
  switchToProject(id) {
    const host = this.host;
    if (id === host.activeProjectId) return false;
    if (!this.storage.temporary && host.activeProjectId != null) this.storage.save();
    if (this.storage.loadProject(id)) {
      host.activeProjectId = id;
      // A reopened server-backed project keeps its identity (outline + write-back).
      const meta = this.storage.store.getMeta(id);
      host.remoteLink = (meta && meta.remoteId && meta.address)
        ? { address: meta.address, remoteId: meta.remoteId, version: meta.remoteVersion || 0 }
        : null;
      host.blankColor = (meta && meta.blank && meta.blankColor) ? meta.blankColor : '';
      this.tabs.reportActive(id);
      host.updateProjectTitle();
      // Server-linked: pull the latest so a reopen shows peers' newest state.
      if (host.remoteLink && getSyncToServer()) this.remoteSync.reloadRemoteActive();
      // Chat persistence (§12): swap in this project's saved chat.
      host.chatPersistence?.projectOpened(id);
      return true;
    }
    return false;
  }

  // Open in a NEW browser tab via a "?open=<id>" deep link (applyProjectDeepLink consumes it).
  openProjectInNewTab(id, win = null) {
    if (id == null) { if (win) win.close(); return; }
    const base = location.origin + location.pathname;
    const url = buildOpenProjectUrl(base, id);
    // `win` was pre-opened inside the user gesture so a strict popup blocker cannot swallow it.
    if (win) win.location = url; else window.open(url, '_blank');
  }

  // A SERVER project in a new tab via the server-launch fragment (applyExternalLaunch consumes it).
  openRemoteProjectInNewTab(meta, win = null) {
    if (!meta || !meta.serverUrl || meta.id == null) { if (win) win.close(); return; }
    const base = location.origin + location.pathname;
    const url = buildExternalLaunchUrl(base, { server: { url: meta.serverUrl, id: meta.id, version: meta.version || 0 } });
    if (win) win.location = url; else window.open(url, '_blank');
  }

  // Load a remote project into the editor, linking the session for live co-edit. A local
  // project already linked to it is switched to instead — never a duplicate local copy.
  async openRemoteProject(meta) {
    const linked = this.storage.store.list().find(m => m.remoteId === meta.id && m.address === meta.serverUrl);
    if (linked) { this.switchToProject(linked.id); return; }
    const conn = requireConnection(this.getConnections(), meta.serverUrl);
    const full = await conn.getProject(meta.id);
    // The server's stored original, else the `source` URL (cross-origin, needs CORS).
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

  // Reset the expiry window to start from now; peers re-render.
  renewProject(id) {
    const meta = this.storage.store.renew(id);
    if (meta) this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    return meta;
  }

  // opts: { expiresAt (0 = keep forever), refreshPeriod, autoRefresh }.
  setProjectExpiration(id, opts = {}) {
    const meta = this.storage.store.setExpiration(id, opts);
    if (meta) {
      this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
      // Only an explicit expiry change propagates to the server (refreshPeriod/autoRefresh
      // are local-only concepts); best-effort, like setProjectColor.
      if (Object.prototype.hasOwnProperty.call(opts, 'expiresAt')) {
        this.pushProjectFieldToServer(id, { expiresAt: meta.expiresAt || 0 }, 'Could not set expiration on the server');
      }
    }
    return meta;
  }

  // Close a project's editor without deleting it; open in ANOTHER tab → a CLOSE broadcast.
  // `fully` also closes this window (only script-opened windows can self-close).
  closeProject(id, { fully = false } = {}) {
    if (id != null && id === this.host.activeProjectId) this.host.newEditor();
    else if (id != null) this.tabs.projectsChanged({ id, action: PROJECT_ACTION.CLOSE });
    if (fully) { try { window.close(); } catch { /* not closeable */ } }
  }

  removeProject(id) {
    if (id === this.host.activeProjectId) {
      this.storage.store.remove(id);
      this.storage.newTemporary();
      this.tabs.reportActive(null);
    } else {
      this.storage.store.remove(id);
    }
    this.host.chatPersistence?.projectRemoved(id);
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.REMOVED });
  }

  clearAllProjects() {
    this.storage.store.clearAll();
    this.host.chatPersistence?.allProjectsCleared();
    this.storage.newTemporary();
    this.tabs.reportActive(null);
    this.tabs.projectsChanged({ action: PROJECT_ACTION.CLEARED });
  }

  // Delegators, not re-exports: callers drive these through the instance that holds the deps.
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
