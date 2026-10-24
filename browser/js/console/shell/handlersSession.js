// The session half of the console's commands: servers and projects, the assistant, the theme,
// the desktop hand-off and the console's own verbs. Each returns the lines to print.
import { getSyncToServer, setSyncToServer } from '../../net/connectionStore.js';
import { openInLaunchPayloadResolved } from '../../core/launch/payload.js';
import { desktopLaunchUrl, inlineVerdict } from '../../core/launch/desktopLink.js';
import { parseLaunchUrl } from '../../core/launch/query.js';
import { applyLaunchQuery } from '../launchQuery.js';
import { loadOpenInConfig } from '../../config/openInConfig.js';
import { openExternalUrl } from '../../utils.js';
import { accentHex } from '../../core/settings/accents.js';
import MOTION from '../../../../common/config/motion.json' with { type: 'json' };

const SPEED = MOTION.consoleSpeed;
// One line naming the reveal speed, shown or set (appearance.zig report).
const speedLine = (speed, showing) => {
  const verb = showing ? 'reveal speed is' : 'reveal speed';
  return speed >= SPEED.max ? `${verb} ${speed} — output appears instantly` : `${verb} ${speed}`;
};
import { parseConnectArgs, splitWords } from './grammar.js';
import { projectsRows, connectionsRows } from './format.js';
import { helpLines } from './help.js';
import { statusLine } from './status.js';

const fold = (s) => String(s ?? '').trim().toLowerCase();
const THEME_WORDS = { dark: 'dark', light: 'light', auto: 'system', system: 'system' };
const LLM_KEYS = { provider: 'provider', url: 'baseUrl', model: 'model', key: 'apiKey' };

// A saved project by id or by name (any casing); null when none matches.
const localProject = (app, ref) => {
  const list = app.storage.store.list();
  return list.find((m) => String(m.id) === ref) ?? list.find((m) => fold(m.name) === fold(ref)) ?? null;
};
const remoteProject = async (stencil, name) => (await stencil.serverProjects()).find((m) => fold(m.name) === fold(name)) ?? null;

export const sessionHandlers = ({ app, stencil }) => ({
  async connect(arg) {
    const targets = parseConnectArgs(arg);
    if (!targets.length) throw new Error("connect needs a server URL — e.g. '/connect http://host:8090 [token]'");
    for (const t of targets) await stencil.connect(t.token ? { url: t.url, token: t.token } : t.url);
    return connectionsRows(stencil.connections);
  },
  disconnect(arg) { stencil.disconnect(arg || undefined); return connectionsRows(stencil.connections); },
  connections() { return connectionsRows(stencil.connections); },
  async projects(arg) {
    const remote = (await stencil.serverProjects()).filter((m) => !arg || m.serverUrl === arg);
    return projectsRows(arg ? [] : app.storage.store.list(), remote, { activeId: app.activeProjectId });
  },
  async open(arg, host) {
    if (!arg) throw new Error("open takes a project name or id, a picture URL, or a launch link");
    const link = parseLaunchUrl(arg);
    if (link) { await applyLaunchQuery({ app, stencil, query: link }); host.statusChanged?.(); return 'opened the launch link'; }
    if (/^https?:\/\//i.test(arg)) { await stencil.load(arg); return `loaded ${arg}`; }
    const local = localProject(app, arg);
    if (local) { app.projectTransfer.switchToProject(local.id); host.statusChanged?.(); return `opened ${local.name}`; }
    const remote = await remoteProject(stencil, arg);
    if (!remote) throw new Error(`no project called '${arg}' — '/projects' lists them`);
    await app.projectTransfer.openRemoteProject(remote);
    return `opened ${remote.name} from ${remote.serverUrl}`;
  },
  async 'open-tab'(arg) {
    const local = localProject(app, arg);
    if (local) { app.projectTransfer.openProjectInNewTab(local.id); return `opened ${local.name} in a new tab`; }
    const remote = await remoteProject(stencil, arg);
    if (!remote) throw new Error(`no project called '${arg}' — '/projects' lists them`);
    app.projectTransfer.openRemoteProjectInNewTab({ ...remote, address: remote.serverUrl, remoteId: remote.id });
    return `opened ${remote.name} in a new tab`;
  },
  async fetch(arg) {
    const [name, url] = splitWords(arg);
    if (!name) throw new Error("fetch takes a server project's name — '/projects' lists them");
    const remote = (await stencil.serverProjects()).find((m) => fold(m.name) === fold(name) && (!url || m.serverUrl === url));
    if (!remote) throw new Error(`no server project called '${name}'`);
    await app.projectTransfer.openRemoteProject(remote);
    return `fetched ${remote.name} from ${remote.serverUrl}`;
  },
  sync(arg) {
    const word = fold(arg);
    if (word === 'on' || word === 'off') setSyncToServer(word === 'on');
    else if (word) throw new Error("sync takes 'on' or 'off'");
    return `sync to server: ${getSyncToServer() ? 'on' : 'off'}`;
  },
  async desktop(arg) {
    const incognito = fold(arg) === 'incognito';
    if (arg && !incognito) throw new Error("desktop takes '[incognito]'");
    const payload = await openInLaunchPayloadResolved(app, { incognito });
    if (!payload) throw new Error('nothing to hand off — open a project first');
    const url = desktopLaunchUrl({ scheme: (await loadOpenInConfig()).desktopScheme, payload, incognito });
    if (inlineVerdict(url, { payload }) === 'refuse') throw new Error('the picture is too large to hand off inline — save it to a server and open that project');
    openExternalUrl(url);
    return 'opened in the desktop app';
  },
  async prompt(arg) {
    if (!arg) throw new Error("prompt needs text — e.g. '/prompt rotate this left and make it b&w'");
    const result = await stencil.prompt(arg);
    return result?.reply ?? result?.text ?? 'done';
  },
  llm(arg) {
    const [key, ...rest] = splitWords(arg);
    if (!key) return ['provider', 'baseUrl', 'model'].map((k) => `${k}: ${stencil.llm[k] || '(unset)'}`);
    const prop = LLM_KEYS[fold(key)];
    if (!prop) throw new Error("llm takes 'provider | url | model | key <value>'");
    if (!rest.length) return `${prop}: ${prop === 'apiKey' ? (stencil.llm.apiKey ? '(set)' : '(unset)') : stencil.llm[prop] || '(unset)'}`;
    if (prop === 'apiKey' && fold(rest.join(' ')) === 'forget') { stencil.llm.apiKey = ''; return 'apiKey forgotten'; }   // the CLI's clear verb
    stencil.llm[prop] = rest.join(' ');
    return `${prop} set`;
  },
  status() { return statusLine(app).text; },
  theme(arg, host) {
    const word = fold(arg);
    if (!word) return [`theme: ${app.accents.themeMode} · accent: ${stencil.mainTheme}`, `modes: ${Object.keys(THEME_WORDS).join(' ')} · accents: ${stencil.mainThemes.join(' ')} #hex`];
    host?.skinOff?.();   // a worn skin comes off first, as the Zig /theme does
    if (THEME_WORDS[word]) { app.accents.setThemeMode(THEME_WORDS[word]); return `theme: ${app.accents.themeMode}`; }
    stencil.mainTheme = arg.trim();
    return `theme set to ${stencil.mainTheme} (${app.customAccent || accentHex(app.accent)})`;
  },
  'reveal-speed'(arg) {
    const word = fold(arg);
    if (!word) return speedLine(stencil.settings.consoleSpeed, true);
    const want = word === 'off' ? SPEED.max : word === 'on' ? SPEED.default : Number(word);
    if (!(want >= SPEED.min && want <= SPEED.max)) return `usage: /reveal-speed [${SPEED.min} … ${SPEED.max}] — ${SPEED.min} slowest, ${SPEED.max} instant (default ${SPEED.default})`;
    app.settings.setMotion('consoleSpeed', want);
    return speedLine(want, false);
  },
  clear(_arg, host) { host.clear(); },
  help(_arg, host) { return helpLines({ cliMode: host.cliMode() }); },
  exit(_arg, host) { host.close(); },
});
