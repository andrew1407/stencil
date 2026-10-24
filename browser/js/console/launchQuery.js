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

// The registry fields: a duration spec sets the expiry from now, anything else is read as a date.
const applyProject = (stencil, fields, problems) => {
  const project = stencil.current;
  if (!project || project.incognito) {
    problems.push('description, keywords, color and expiry need a saved project — the editor is incognito or empty');
    return;
  }
  const steps = {
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

const attempt = async (problems, label, run) => {
  try { await run(); } catch (err) { problems.push(`${label} — ${err?.message || err}`); }
};

// Reads app.launchQuery (parsed by the DrawingApp constructor) and strips the URL at once so a
// reload repeats nothing — or a `query` typed at the console, which strips nothing — then applies
// it in order: look, assistant, servers, picture, layout, project, chat.
export const applyLaunchQuery = async ({ app, stencil, query = null }) => {
  const q = query ?? app.launchQuery;
  if (!q) return;
  if (!query) {
    app.launchQuery = null;
    history.replaceState(null, '', location.pathname + (q.fromHash ? '' : location.hash));
  }

  const problems = q.warnings.map((w) => WARNING_TEXT[w.code](w.key));
  if (q.theme) await attempt(problems, 'theme', () => app.accents.setThemeMode(q.theme));
  applySettings(stencil, q.settings, problems);
  if (Object.keys(q.llm).length) await attempt(problems, 'assistant', () => stencil.llm.setup(q.llm));
  for (const { url, token } of q.connect) {
    await attempt(problems, `connect ${url}`, () => ensureLaunchConnection(app, url, { token, intent: CONNECT_INTENT }));
  }
  await attempt(problems, 'picture', () => openPicture({ app, stencil }, q));
  if (q.layout) {
    if (app.image) await attempt(problems, 'layout', () => applyLayout(stencil, q.layout));
    else problems.push('layout needs a picture — add source= or blank=');
  }
  if (Object.values(q.project).some((v) => v != null)) applyProject(stencil, q.project, problems);
  if (q.script && !app.pendingLaunchScript) app.pendingLaunchScript = q.script;
  if (q.chat.dock) await attempt(problems, 'chat', () => stencil.chat.dock(q.chat.dock));
  if (q.chat.open) await attempt(problems, 'chat', () => stencil.chat.open());
  for (const p of problems) notify(`Launch link: ${p}`, 'info');
};
