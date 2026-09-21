// The project-meta writes (rename, colour, keywords, description, blank colour) and the
// version-guarded single-field server push they all ride. `c` is the ProjectTransferController.
import { notify } from '../../../utils.js';
import { PROJECT_ACTION } from '../../../worker/messages.js';
import { normalizeHex } from '../../settings/accents.js';
import { requireConnection } from '../../../net/remoteSync.js';
import { getSyncToServer } from '../../../net/connectionStore.js';

// Registry meta is the source of truth for the name, and save() prefers it over
// imageBaseName. Returns updated meta (null for unknown id).
export function renameProject(c, id, name) {
  const clean = String(name || '').trim();
  if (!clean) return null;
  // Names are unique; null reads as "kept old name" in the UI (the console checks nameExists first).
  if (c.storage.store.nameExists(clean, id)) {
    notify(`A project named “${clean}” already exists`, 'fail');
    return null;
  }
  const meta = c.storage.store.rename(id, clean);
  if (meta) {
    // imageBaseName (the working/download name) follows the project name for the active project.
    if (id === c.host.activeProjectId) {
      c.host.imageBaseName = clean;
      c.host.updateProjectTitle();
    }
    c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    pushProjectFieldToServer(c, id, { name: clean }, 'Could not rename the project on the server');
  }
  return meta;
}

// Version-guarded, best-effort (no-op when not linked / sync off; a failure only notifies
// with `failMsg`). The active project uses its live remoteLink and adopts the bumped version.
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
  // Bounded conflict retry (mirrors the CLI's putProjectField): a stale cached version 409s,
  // so re-read and retry. Single-field sets are idempotent, so last-writer-wins is correct.
  let version = active ? host.remoteLink.version : (meta.remoteVersion || 0);
  for (let attempt = 0; attempt < 4; attempt++) {
    try {
      const rec = await conn.updateProject(remoteId, { ...fields, version });
      // Only if remoteLink still points at this project (the user may have switched during the await).
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

// After a 409 or a file write that bumps the version without returning it.
export async function currentRemoteVersion(c, conn, remoteId, fallback) {
  try {
    const full = await conn.getProject(remoteId);
    const v = full && full.project ? full.project.version : undefined;
    return v == null ? fallback : v;
  } catch {
    return fallback;
  }
}

// The custom colour the project's NAME is painted in. Empty clears it (theme accent);
// invalid hex is rejected. Returns updated meta (null for unknown id).
export function setProjectColor(c, id, color) {
  const raw = String(color == null ? '' : color).trim();
  let next = '';
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
  pushProjectFieldToServer(c, id, { color: next }, 'Could not set project color on the server');
  return meta;
}

// Keywords are normalized by the store. Null on unknown id.
export function setProjectKeywords(c, id, keywords) {
  const meta = c.storage.store.setKeywords(id, keywords);
  if (!meta) return null;
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  pushProjectFieldToServer(c, id, { keywords: meta.keywords }, 'Could not set project keywords on the server');
  return meta;
}

// Trimmed by the store; '' clears. Null on unknown id.
export function setProjectDescription(c, id, description) {
  const meta = c.storage.store.setDescription(id, description);
  if (!meta) return null;
  c.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
  pushProjectFieldToServer(c, id, { description: meta.description }, 'Could not set project description on the server');
  return meta;
}

// Null for a non-blank project. The ACTIVE project recolours in place (setBlankColor);
// otherwise stored meta + peers + server.
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