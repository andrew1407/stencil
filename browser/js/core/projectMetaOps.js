// Ported from js/core/projectTransferController.js: the project-meta writes (rename, colour,
// keywords, description, blank colour) and the version-guarded single-field server push they
// all ride. `c` is the ProjectTransferController — these read its storage/tabs/host deps.
import { notify } from '../utils.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import { normalizeHex } from './accents.js';
import { requireConnection } from '../net/remoteSync.js';
import { getSyncToServer } from '../net/connectionStore.js';

// Rename a project. Registry meta is the source of truth for the projects list, and
// save()'s name fallback prefers it over imageBaseName, so an active-project rename
// survives saves. Notifies peers to re-render. Returns updated meta (null for unknown id).
export function renameProject(c, id, name) {
  const clean = String(name || '').trim();
  if (!clean) return null;
  // Names must be unique across projects. The UI surfaces null as "kept old name";
  // the console's Project.name setter checks store.nameExists() first to throw.
  if (c.storage.store.nameExists(clean, id)) {
    notify(`A project named “${clean}” already exists`, 'fail');
    return null;
  }
  const meta = c.storage.store.rename(id, clean);
  if (meta) {
    // The project name is THE name: keep the working/download name (imageBaseName)
    // in lockstep for the active project, no matter which surface renamed it
    // (topbar, projects list, links modal, console). No separate image name to track.
    if (id === c.host.activeProjectId) {
      c.host.imageBaseName = clean;
      c.host.updateProjectTitle();   // refresh tab title + topbar field
    }
    c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    // Push the rename to the server immediately (like setProjectColor), so peers see it live.
    pushProjectFieldToServer(c, id, { name: clean }, 'Could not rename the project on the server');
  }
  return meta;
}

// Push a single field change (rename / colour) to the collaboration server for a
// server-linked project. The active project uses its live remoteLink (and adopts the bumped
// version); a non-active linked project uses its stored meta. Version-guarded + best-effort
// (no-op when not linked / sync off) — a failure only notifies with `failMsg`.
export async function pushProjectFieldToServer(c, id, fields, failMsg) {
  if (!getSyncToServer()) return;
  const host = c.host;
  const active = id === host.activeProjectId && !!host.remoteLink;
  const meta = c.storage.store.getMeta(id) || {};
  const address = active ? host.remoteLink.address : meta.address;
  const remoteId = active ? host.remoteLink.remoteId : meta.remoteId;
  if (!address || !remoteId) return;
  let conn;
  try {
    conn = requireConnection(c.getConnections(), address);
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
        version = await currentRemoteVersion(c, conn, remoteId, version);
        continue;
      }
      notify(`${failMsg} — ${err.message}`, 'fail');
      return;
    }
  }
}

// Re-read a linked project's current server version (after a 409 or a file write
// that bumps it without returning it), falling back to `fallback` on any error.
export async function currentRemoteVersion(c, conn, remoteId, fallback) {
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
export function setProjectColor(c, id, color) {
  const raw = String(color == null ? '' : color).trim();
  let next = '';   // empty → explicit clear (theme fallback)
  if (raw) {
    next = normalizeHex(raw);
    if (!next) {
      notify(`“${color}” is not a valid hex color`, 'fail');
      return null;
    }
  }
  const meta = c.storage.store.setColor(id, next);
  if (!meta) return null;
  if (id === c.host.activeProjectId) c.host.updateProjectTitle();
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  // Best-effort server push for a server-linked project (no-op when not linked).
  pushProjectFieldToServer(c, id, { color: next }, 'Could not set project color on the server');
  return meta;
}

// Set a project's search keywords (normalized by the store). Mirrors setProjectColor:
// writes local meta, broadcasts to peer tabs, and best-effort pushes to the server for a
// server-linked project. Returns the stored meta, or null on unknown id.
export function setProjectKeywords(c, id, keywords) {
  const meta = c.storage.store.setKeywords(id, keywords);
  if (!meta) return null;
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  pushProjectFieldToServer(c, id, { keywords: meta.keywords }, 'Could not set project keywords on the server');
  return meta;
}

// Set a project's free-text description (trimmed by the store; '' clears). Same shape as
// setProjectKeywords: local meta, peer tabs, best-effort server push. Null on unknown id.
export function setProjectDescription(c, id, description) {
  const meta = c.storage.store.setDescription(id, description);
  if (!meta) return null;
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  pushProjectFieldToServer(c, id, { description: meta.description }, 'Could not set project description on the server');
  return meta;
}

// Set a project's blank-fill colour by id. No-op (null) for a non-blank project (only blanks have
// a blank colour). When `id` is the ACTIVE project, recolours the visible background in place
// (setBlankColor); otherwise updates the stored meta + peers + server. `color` is any normalizeHex
// form. Returns the stored meta, or null.
export function setProjectBlankColor(c, id, color) {
  const cur = c.storage.store.getMeta(id);
  if (!cur || !cur.blank) return null;
  const next = normalizeHex(color);
  if (!next) return null;
  if (id === c.host.activeProjectId) { c.host.setBlankColor(next); return c.storage.store.getMeta(id); }
  const meta = c.storage.store.setBlankColor(id, next);
  if (!meta) return null;
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  pushProjectFieldToServer(c, id, { blankColor: meta.blankColor }, 'Could not set blank color on the server');
  return meta;
}