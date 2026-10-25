// Applies the launch query (core/launch/query.js) as a typed console line would: each setting,
// connection, picture and project field goes through window.stencil, and only what the facade
// has no verb for (the theme mode, a server confirm, an incognito name) reaches the app itself.
// Consumed once at boot, after the `#stencil=` hand-off; it never rejects.
import { notify } from '../utils.js';
import { guardedFetch } from '../net/fetchGuard.js';
import { readJsonCapped } from '../net/cappedBody.js';
import { parseDuration } from '../core/parse/durationParser.js';
import { normalizeUrl } from '../net/urlRules.js';
import { ensureLaunchConnection, applyServerLaunch } from '../core/launch/controller.js';
import { reportIncognitoSession } from '../core/launch/incognitoFlow.js';
import { loadLlmSettings, withSessionKey } from '../llm/settings.js';
import { splitKeywords } from './coerce.js';

const CONNECT_INTENT = { does: 'connects to', title: 'Connect to server' };

const WARNING_TEXT = {
  'secret-in-query': (k) => `"${k}" is ignored in the ?query — put it in the #fragment, which never reaches a server`,
  'bad-json': (k) => `"${k}" is not a JSON object`,
  'bad-value': (k) => `"${k}" has an unusable value`,
};

// Every setting lands on its own setter, so one bad value never blocks the next.
const applySettings = (stencil, settings, problems) => {
  const set = stencil.settings;
  const unknown = [];
  for (const [key, value] of Object.entries(settings)) {
    if (typeof Object.getOwnPropertyDescriptor(set, key)?.set !== 'function') { unknown.push(key); continue; }
    try { set[key] = value; } catch (err) { problems.push(`${key} — ${err.message}`); }
  }
  if (unknown.length) problems.push(`unknown option${unknown.length > 1 ? 's' : ''} ${unknown.join(', ')}`);
};

// A saved project is renamed through its handle; an incognito editor has only the picture's name.
const renameCurrent = ({ app, stencil }, name) => {
  const project = stencil.current;
  if (project && !project.incognito) {
    project.name = name;
    project.imageName = name;
    return;
  }
  app.imageBaseName = name;
  reportIncognitoSession(app);
  app.updateProjectTitle?.();
};

// A link may re-point the assistant, or swap the key it bills, only after the user agrees; the key is never shown.
const ENDPOINT_KEYS = ['provider', 'baseUrl', 'serverUrl'];
const applyAssistant = async ({ app, stencil }, llm) => {
  const cur = withSessionKey(loadLlmSettings());
  const moves = ENDPOINT_KEYS.filter((k) => llm[k] != null && llm[k] !== cur[k]).map((k) => `${k} ${llm[k]}`);
  if (llm.apiKey && llm.apiKey !== cur.apiKey) moves.push('a different API key');
  if (moves.length) {
    const where = moves.join(', ');
    const ok = await app.confirm(
      `This link changes the assistant (${where}). Your prompts and pictures would go there. Apply it?`,
      { title: 'Change the assistant', confirmLabel: 'Apply', confirmIcon: 'chat-settings' });
    if (!ok) throw new Error('left unchanged — the change was declined');
  }
  stencil.llm.setup(llm);
};

const sameOrigin = (a, b) => {
  try { return normalizeUrl(a) === normalizeUrl(b); } catch { return false; }
};

// `project=`: a server's project by id or name once connected (the locator's server, else the
// launch's), or a local project when no server is named at all.
const openProjectRef = async ({ app, stencil }, q) => {
  const { ref, server } = q.picture;
  const origin = server ?? q.connect[0]?.url ?? null;
  if (!origin) {
    const byName = stencil.getProjectByName(ref);
    if (byName) { byName.open(); return; }
    if (!app.projectTransfer.switchToProject(ref)) throw new Error(`no local project "${ref}"`);
    return;
  }
  const token = q.connect.find((c) => sameOrigin(c.url, origin))?.token ?? '';
  const url = await ensureLaunchConnection(app, origin, { token });
  if (!url) return;
  const match = (await stencil.serverProjects())
    .find((m) => m.serverUrl === url && (m.id === ref || String(m.name).toLowerCase() === ref.toLowerCase()));
  if (!match) throw new Error(`no project "${ref}" on ${url}`);
  await applyServerLaunch(app, { server: { url, id: match.id, version: match.version || 0 }, incognito: q.incognito });
};

// Each picture kind lands on the facade call a typed line would make; a source or a blank makes
// the picture `name=` renames.
const PICTURE_OPENERS = {
  server: ({ app }, { picture, incognito }) => applyServerLaunch(app, { server: picture, incognito }),
  project: openProjectRef,
  source: ({ stencil }, { picture, incognito }) =>
    stencil.load(picture.url, picture.frame != null ? { incognito, frame: picture.frame } : { incognito }),
  blank: ({ stencil }, { picture, incognito }) => {
    if (incognito) stencil.incognito = true;
    const size = { width: picture.width ?? undefined, height: picture.height ?? undefined };
    return stencil.blank(picture.color, { size });
  },
};
const NAMED_KINDS = new Set(['source', 'blank']);

const openPicture = async (ctx, q) => {
  const { picture, incognito, name } = q;
  if (!picture) {
    if (incognito) ctx.stencil.incognito = true;
    return;
  }
  await PICTURE_OPENERS[picture.kind](ctx, q);
  if (name && NAMED_KINDS.has(picture.kind)) renameCurrent(ctx, name);
};

const applyLayout = async (stencil, layout) => {
  let data = layout.data;
  if (!data) {
    const resp = await guardedFetch(layout.url);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    data = await readJsonCapped(resp);
  }
  stencil.applyLayout(data);
};

// On a server project a link's fields stay on this machine and its expiry is ignored: one click
// must not rewrite or expire a project collaborators share (launch-uri.md §2).
const LOCAL = { local: true };
const serverSteps = (app, project) => ({
  description: (v) => { app.projectTransfer.setProjectDescription(project.id, v, LOCAL); },
  keywords: (v) => { app.projectTransfer.setProjectKeywords(project.id, splitKeywords(v), LOCAL); },
  color: (v) => { if (app.projectTransfer.setProjectColor(project.id, v, LOCAL) == null) throw new Error(`"${v}" is not a colour`); },
  expire: () => { throw new Error('ignored — a link never expires a server project'); },
});

// The registry fields: a duration spec sets the expiry from now, anything else is read as a date.
const applyProject = ({ app, stencil }, fields, problems) => {
  const project = stencil.current;
  if (!project || project.incognito) {
    problems.push('description, keywords, color and expiry need a saved project — the editor is incognito or empty');
    return;
  }
  const steps = app.remoteLink ? serverSteps(app, project) : {
    description: (v) => { project.description = v; },
    keywords: (v) => { project.keywords = v; },
    color: (v) => { project.color = v; },
    expire: (v) => { if (parseDuration(v) != null) project.expire(v); else project.expiresAt = v; },
  };
  for (const [key, run] of Object.entries(steps)) {
    if (fields[key] == null) continue;
    try { run(fields[key]); } catch (err) { problems.push(`${key} — ${err.message}`); }
  }
};

const framed = () => {
  try { return typeof window !== 'undefined' && window.parent != null && window.parent !== window; } catch { return true; }
};
const CONNECTING = new Set(['server', 'project']);

// Framed, a host page can dress the confirm buttons up as its own: only the picture and the look apply.
const framedOnly = (q, problems) => {
  const picture = CONNECTING.has(q.picture?.kind) ? null : q.picture;
  const project = Object.values(q.project).some((v) => v != null);
  if (!q.connect.length && !Object.keys(q.llm).length && !project && picture === q.picture) return q;
  problems.push('inside a frame, its servers, assistant and project fields are left out');
  return { ...q, connect: [], llm: {}, project: {}, picture };
};

const attempt = async (problems, label, run) => {
  try { await run(); } catch (err) { problems.push(`${label} — ${err?.message || err}`); }
};

// Reads app.launchQuery (parsed by the DrawingApp constructor) and strips the URL at once so a
// reload repeats nothing — or a `query` typed at the console, which strips nothing — then applies
// it in order: look, assistant, servers, picture, layout, project, chat.
export const applyLaunchQuery = async ({ app, stencil, query = null }) => {
  let q = query ?? app.launchQuery;
  if (!q) return;
  if (!query) {
    app.launchQuery = null;
    history.replaceState(null, '', location.pathname + (q.fromHash ? '' : location.hash));
  }

  const problems = q.warnings.map((w) => WARNING_TEXT[w.code](w.key));
  if (!query && framed()) q = framedOnly(q, problems);
  if (q.theme) await attempt(problems, 'theme', () => app.accents.setThemeMode(q.theme));
  applySettings(stencil, q.settings, problems);
  if (Object.keys(q.llm).length) await attempt(problems, 'assistant', () => applyAssistant({ app, stencil }, q.llm));
  for (const { url, token } of q.connect) {
    await attempt(problems, `connect ${url}`, () => ensureLaunchConnection(app, url, { token, intent: CONNECT_INTENT }));
  }
  await attempt(problems, 'picture', () => openPicture({ app, stencil }, q));
  if (q.layout) {
    if (app.image) await attempt(problems, 'layout', () => applyLayout(stencil, q.layout));
    else problems.push('layout needs a picture — add source= or blank=');
  }
  if (Object.values(q.project).some((v) => v != null)) applyProject({ app, stencil }, q.project, problems);
  if (q.script && !app.pendingLaunchScript) app.pendingLaunchScript = q.script;
  if (q.chat.dock) await attempt(problems, 'chat', () => stencil.chat.dock(q.chat.dock));
  if (q.chat.open) await attempt(problems, 'chat', () => stencil.chat.open());
  for (const p of problems) notify(`Launch link: ${p}`, 'info');
};
