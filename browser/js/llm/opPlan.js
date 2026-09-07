// ── Op-plan: system prompt, parser/validator, and executor (llm-contract.md §1–4) ──
// Pure module — no DOM, no fetch. The LLM never touches pixels: it emits a strictly
// validated plan of whitelisted ops that executeOpPlan maps 1:1 onto the same
// window.stencil facade calls the toolbar uses. LLM output is data, not instructions.
import { defaultBlankSizePx } from '../core/layout.js';
import { createSchema } from './opSchema.js';
import PROMPT_ASSET from '../config/llm/systemPrompt.json' with { type: 'json' };
import REGISTRY from '../config/llm/opRegistry.json' with { type: 'json' };

// Canonical system prompt (contract §4 + §13): the PROSE CORE (head+tail from the shared
// config/llm/systemPrompt.json asset) is embedded verbatim — append-only, never prepend —
// while the "Available ops" section and the §10 settings block are ASSEMBLED from the OPS
// registry, so the prompt can never promise an op this surface cannot run.
export const PROMPT_CORE_HEAD = PROMPT_ASSET.head;
export const PROMPT_CORE_TAIL = PROMPT_ASSET.tail;

// The table this surface validates against: config/llm/opRegistry.json, filtered to the
// browser's profile (contract §13). Membership, key schemas, caps, token grammars and
// the cross-field rules all come from the registry; this module keeps the executors.
export const SCHEMA = createSchema(REGISTRY, 'browser');

// Limits — the same numbers in every client (contract §1), read from the registry.
export const LIMITS = {
  actions: SCHEMA.limits.MAX_ACTIONS, variants: SCHEMA.limits.MAX_VARIANTS,
  layoutLines: SCHEMA.limits.MAX_LAYOUT_LINES, frameIndices: SCHEMA.limits.MAX_FRAME_INDICES,
  stringChars: SCHEMA.limits.MAX_STRING_CHARS, pathChars: SCHEMA.limits.MAX_PATH_CHARS,
};

// §11 interactive replies. The option cap is what a choice card can show without becoming a
// menu; question/label/answer caps keep a model-written card from filling the transcript.
export const ASK_LIMITS = { ...SCHEMA.limits.ask };
export const DEFAULT_CUSTOM_LABEL = REGISTRY.ask.defaultCustomLabel;

// ── §1 coordinate re-mapping (executor-side) ────────────────────────────────
// Plan coordinates are in the frame of the working image the model SAW; crop/rotate change
// it mid-plan, so the executor composes a running affine map (quarter turns + integer
// translations, exact arithmetic) and pushes every later layout point through it, clamped.
const identityFrame = () => ({ a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 });

// Compose `n` AFTER `f` (points flow f → n), in place.
const composeFrame = (f, n) => {
  const { a, b, c, d, tx, ty } = f;
  f.a = n.a * a + n.b * c;
  f.b = n.a * b + n.b * d;
  f.tx = n.a * tx + n.b * ty + n.tx;
  f.c = n.c * a + n.d * c;
  f.d = n.c * b + n.d * d;
  f.ty = n.c * tx + n.d * ty + n.ty;
};

const mapFramePoint = (f, p) => ({ x: f.a * p.x + f.b * p.y + f.tx, y: f.c * p.x + f.d * p.y + f.ty });

const isObj = (v) => v != null && typeof v === 'object' && !Array.isArray(v);
const isStr = (v, max = LIMITS.stringChars) => typeof v === 'string' && v.length <= max;

// ── Browser-side normalizers (the few outputs the generic deep-pick can't express) ──
// The generic normalize() yields { op, ...declared keys, defaults } with `trim` keys
// trimmed; these run on top of it.
const NORMALIZE = {
  // "" after trimming is no destination at all.
  save: (out) => { if (out.path === '') delete out.path; return out; },
  // A preset persists by its lowercase key; the default "image" form is the bare op.
  accent: (out) => { if (out.preset != null) out.preset = out.preset.toLowerCase(); return out; },
  copy: (out) => (out.what === 'layout' ? out : { op: 'copy' }),
};

// ── The executors: one per registered op (contract §13) ──
// run(a, ctx) executes a VALIDATED action against the frozen facade. Everything else about
// an op — its prompt bullet, "also accepts" line, flags, capability requirements and key
// schema — is the registry entry's, assembled into OPS below.
const RUN = {
  crop: (a, { stencil, frame }) => {
    // The crop path resolves the spec itself; we only observe the resolved rect.
    // cropRect before/after are both in rotated-original px, so their origin delta IS
    // the rect's origin in the pre-crop frame: later plan coords shift by its negation (§1).
    const before = frame && stencil.cropRect;
    stencil.crop(a.spec);
    const after = frame && stencil.cropRect;
    if (before && after) {
      composeFrame(frame, { a: 1, b: 0, c: 0, d: 1, tx: -(after.x - before.x), ty: -(after.y - before.y) });
    }
  },
  rotate: (a, { stencil, frame }) => {
    for (let i = 0; i < a.times; i++) {
      const size = stencil.imageSize;   // pre-turn W×H — the box the points rotate in
      (a.dir === 'left' ? stencil.rotateLeft() : stencil.rotateRight());
      // Same quarter-turn the editor applies to existing points
      // (cropGeometry rotateLinePointsQuarter): right x'=H−y,y'=x; left x'=y,y'=W−x.
      if (frame && size) {
        composeFrame(frame, a.dir === 'right'
          ? { a: 0, b: -1, c: 1, d: 0, tx: size.height, ty: 0 }
          : { a: 0, b: 1, c: -1, d: 0, tx: 0, ty: size.width });
      }
    }
  },
  filter: (a, { stencil }) => {
    stencil.apply(a.mode === 'custom' ? { filter: 'custom', filterColor: a.tint } : { filter: a.mode });
  },
  layout: (a, { stencil, frame }) => {
    // setLines(), not `stencil.layout =`: the latter is the clipboard-paste path and
    // prompts Combine / Replace / Cancel once the image carries lines — a dialog a
    // plan cannot answer. Merges the current image dims itself.
    if (!stencil.imageSize) throw new Error('Layout actions need a working image to draw on');
    // §1: re-map each point through the plan's accumulated crop/rotate transform,
    // then clamp into the working image — even at identity, so out-of-frame model
    // points can never draw outside the picture. The validated action is not mutated.
    const { width, height } = stencil.imageSize;
    const clamp = (v, hi) => Math.min(Math.max(v, 0), hi);
    const lines = a.lines.map((l) => ({
      ...l,
      points: l.points.map((p) => {
        const q = frame ? mapFramePoint(frame, p) : p;
        return { x: clamp(q.x, width), y: clamp(q.y, height) };
      }),
    }));
    stencil.setLines(lines);
  },
  formula: (a, { stencil }) => {
    if (a.enabled != null) { stencil.apply({ allowFormulas: a.enabled }); return; }
    const key = a.axis === 'x' ? 'formulaX' : 'formulaY';
    // An empty expr clears the axis without switching formulas on for it.
    stencil.apply(a.expr.trim() ? { allowFormulas: true, [key]: a.expr } : { [key]: '' });
  },
  page: (a, { stencil }) => {
    if (a.format) { stencil.apply({ page: a.format }); return; }
    // Custom dims map onto the editors' custom page size (contract §2): the two
    // cm setters, then the 'custom' page format — the same controls' paths.
    stencil.pageWidth = a.width;
    stencil.pageHeight = a.height;
    stencil.apply({ page: 'custom' });
  },
  blank: async (a, { stencil }) => {
    if (a.format) stencil.apply({ page: a.format });
    // Explicit cm dims override the format: rendered at the same 96 dpi a page-
    // format blank gets (core defaultBlankSizePx), passed as the pixel size opts.
    if (a.width != null) await stencil.blank(a.color, { size: defaultBlankSizePx({ width: a.width, height: a.height }) });
    else await stencil.blank(a.color);
  },
  // §2 undo/redo: the surface's OWN edit history, one facade step per history entry.
  // Top-level only — sandboxed variant/preview renders write history-invisible state,
  // so stepping history from inside one would tear the sandbox open.
  undo: (a, { stencil }) => { for (let i = 0; i < a.steps; i++) stencil.undo(); },
  redo: (a, { stencil }) => { for (let i = 0; i < a.steps; i++) stencil.redo(); },
  frame: async (a, { exportImage, loadFrame, results }) => {
    if (!loadFrame) throw new Error('The current input is not a video — the "frame" operation needs a video input');
    if (a.index != null) { await loadFrame(a.index); return; }
    // Multiple indices → one extra output image per frame (like variants).
    for (const idx of a.indices) {
      await loadFrame(idx);
      if (exportImage) results.push({ label: `frame${idx}`, dataUrl: await exportImage() });
    }
  },

  // ── §2.1 multi-image ops: switch to a turn attachment / persist the result.
  // topLevelOnly shares the editor-settings enforcement (never inside variants or
  // ask previews) without being settings ops. ──
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

// ── The op registry: one entry per whitelisted op (contract §13) ──
// Assembled from the registry's browser entries, in registry order (which IS the prompt's
// bullet order): bullet / also / alsoOrder / requires / flags are the entry's own;
// validate(a) is the table-driven check + normalize; run(a, ctx) the executor above.
// editorSetting marks §10 ops (adjust the EDITOR, not the image — forbidden in variants).
export const OPS = {};
for (const entry of SCHEMA.entries) {
  if (!RUN[entry.name]) throw new Error(`opRegistry: the browser registers "${entry.name}" but has no executor for it`);
  const def = {
    bullet: entry.bullet ?? null,
    validate(a) {
      const out = SCHEMA.normalize(SCHEMA.validateAction(a, entry), entry);
      return NORMALIZE[entry.name] ? NORMALIZE[entry.name](out) : out;
    },
    run: RUN[entry.name],
  };
  if (entry.also) { def.also = entry.also; def.alsoOrder = entry.alsoOrder; }
  if (entry.requires) def.requires = entry.requires.slice();
  for (const flag of ['editorSetting', 'topLevelOnly', 'newFrame', 'deferred']) {
    if (entry.flags && entry.flags[flag]) def[flag] = true;
  }
  OPS[entry.name] = def;
}
for (const name of Object.keys(RUN)) {
  if (!OPS[name]) throw new Error(`opRegistry: executor "${name}" has no browser registry entry`);
}

// ── §13 registry-driven prompt assembly ─────────────────────────────────────

// Every capability the browser chat surface wires (chatSession.js), so the shipped prompt
// promises every registered op. assemblePrompts() with a reduced set drops bullets of ops
// whose capability is missing (§13 capability truth) — those fall to §1's unknown-op skip.
export const BROWSER_CAPABILITIES = new Set([
  'loadAttachment', 'saveProject', 'removeProjectNamed', 'clearLocalProjects',
  'renameActiveProject', 'setBlankColor', 'openProjectNamed', 'clearChatConversation',
  'setChatPlacement', 'openDialog', 'setVoiceChat',
]);

// §13 forbidden ops — the "never model-drivable" boundary, one name list per the
// contract's categories. Two enforcement teeth: a test that no OPS key uses one of
// these names, and the executor-level reject in executeOpPlan.
// The names are the registry's forbidden.perSurface.browser list: llm/provider
// configuration, clipboard reads, hotkey rebinding, session end, chat persistence/consent
// toggles (clearing is the clearChat op) and sharing scope beyond what §10 grants.
export const FORBIDDEN_OPS = new Set(SCHEMA.forbidden);

// The typed error the executor throws for a forbidden op — even one that somehow
// bypassed the parser (which drops unknown names before they get here).
export class ForbiddenOpError extends Error {
  constructor(op) {
    super(`Forbidden operation "${op}" — this op is never model-drivable (contract §13)`);
    this.name = 'ForbiddenOpError';
    this.op = op;
  }
}

// §13 prompt censor: no generated bullet may read like provider/secret plumbing.
// \btoken\b (not a bare substring) so crop's legitimate "crop tokens" prose passes.
const CENSORED = /api\s*key|bearer|\btoken\b|base\s*url|endpoint/i;

// Assemble the §4 "Available ops" section and §10 settings block from the registry: entry
// order IS bullet order; "also accepts" lines follow the settings bullets (alsoOrder);
// entries whose `requires` are not all in `available` are excluded — never promised.
export const assemblePrompts = (available = BROWSER_CAPABILITIES) => {
  const coreBullets = [], settingsBullets = [], alsoEntries = [];
  for (const def of Object.values(OPS)) {
    if (def.requires && !def.requires.every((c) => available.has(c))) continue;
    if (def.bullet) (def.editorSetting ? settingsBullets : coreBullets).push(def.bullet);
    if (def.also) alsoEntries.push(def);
  }
  alsoEntries.sort((a, b) => a.alsoOrder - b.alsoOrder);
  const settings = [...settingsBullets, ...alsoEntries.map((d) => d.also)];
  for (const b of [...coreBullets, ...settings]) {
    if (CENSORED.test(b)) throw new Error(`Prompt censor: a registry bullet matches a sensitive pattern (api key/bearer/token/base url/endpoint): ${JSON.stringify(b.slice(0, 60))}`);
  }
  return {
    systemPrompt: PROMPT_CORE_HEAD + coreBullets.join('\n') + PROMPT_CORE_TAIL,
    settingsPrompt: settings.join('\n'),
  };
};

const ASSEMBLED = assemblePrompts();
// Canonical §4 prompt and the §10 editor-settings block, assembled at module load —
// clients may append a short dynamic suffix but never prepend anything.
export const LLM_SYSTEM_PROMPT = ASSEMBLED.systemPrompt;
export const EDITOR_SETTINGS_PROMPT = ASSEMBLED.settingsPrompt;

// The browser editor's full system prompt: §4 + the §10 block at the OP LIST's end — which
// is the `ask` paragraph's start, since §11 sits between the ops and the chat-only note.
// The anchor is verified: rewording §4 without updating it must fail loudly here, not
// silently ship an editor prompt with the §10 block missing.
const SETTINGS_SPLICE_ANCHOR = '\n\nWhen a choice is genuinely';
if (!LLM_SYSTEM_PROMPT.includes(SETTINGS_SPLICE_ANCHOR)) {
  throw new Error('LLM_SYSTEM_PROMPT no longer contains the editor-settings splice anchor');
}
export const EDITOR_SYSTEM_PROMPT = LLM_SYSTEM_PROMPT.replace(
  SETTINGS_SPLICE_ANCHOR,
  `\n${EDITOR_SETTINGS_PROMPT}${SETTINGS_SPLICE_ANCHOR}`);

// Derived from the registry: one appearing inside a variant fails the WHOLE plan.
const EDITOR_SETTINGS_OPS = new Set(Object.keys(OPS).filter((op) => OPS[op].editorSetting));
// §2.1 image/save ride the same top-level-only enforcement (the message differs).
const TOP_LEVEL_ONLY_OPS = new Set(Object.keys(OPS).filter((op) => OPS[op].editorSetting || OPS[op].topLevelOnly));

// §1: a misplaced op inside a variant / ask preview. Typed so callers can DROP that one
// variant (or that option's preview) with a warning instead of failing the whole plan.
export class MisplacedOpError extends Error {
  constructor(reason) {
    super(`Invalid plan: ${reason}`);
    this.name = 'MisplacedOpError';
    this.reason = reason;
  }
}

// Validate one actions list: unknown ops drop with a warning (forward compatibility);
// a known op with invalid params throws — nothing executes (contract §1). `scope`
// (non-null = inside a variant/preview) names where the op landed, for the message.
const validateActions = (list, warnings, where, scope = null) => {
  if (list == null) return [];
  if (!Array.isArray(list)) throw new Error(`Invalid plan: ${where} must be an array`);
  if (list.length > LIMITS.actions) throw new Error(`Invalid plan: more than ${LIMITS.actions} actions in ${where}`);
  const out = [];
  for (const a of list) {
    if (!isObj(a) || typeof a.op !== 'string') throw new Error(`Invalid plan: every action in ${where} must be an object with an "op"`);
    if (scope && TOP_LEVEL_ONLY_OPS.has(a.op)) {
      throw new MisplacedOpError(EDITOR_SETTINGS_OPS.has(a.op)
        ? `editor-settings op "${a.op}" is not allowed inside ${scope}`
          + (a.op === 'openUrl' ? " — open the URL as a top-level action; picking images off a web page is the extension assistant's job" : '')
        : a.op === 'undo' || a.op === 'redo'
          ? `"${a.op}" steps the live edit history — a top-level action only, not allowed inside variants or previews`
          : `"${a.op}" is a top-level action only (§2.1) — not allowed inside ${scope}`);
    }
    const def = OPS[a.op];
    if (!def) { warnings.push(`Skipped unknown operation "${a.op}"`); continue; }
    out.push(def.validate(a));
  }
  return out;
};

// Take the first balanced { … } object (string-aware) from the text, or null.
const firstJsonObject = (text) => {
  const start = text.indexOf('{');
  if (start < 0) return null;
  let depth = 0, inStr = false, esc = false;
  for (let i = start; i < text.length; i++) {
    const c = text[i];
    if (inStr) {
      if (esc) esc = false;
      else if (c === '\\') esc = true;
      else if (c === '"') inStr = false;
      continue;
    }
    if (c === '"') inStr = true;
    else if (c === '{') depth++;
    else if (c === '}') { depth--; if (depth === 0) return text.slice(start, i + 1); }
  }
  return null;
};

// ── §11 interactive replies (`ask`) ─────────────────────────────────────────
// A question put back to the user as a choice card. Validated as strictly as an action:
// a card nobody can answer (too few/many options, an option that is both a render AND a
// reference) is a plan error, not something to paper over.
// Validate the optional `ask` object → the normalised card, or null when absent. The
// card's STRUCTURE (keys, caps, the image reference's exactly-one-of url / projectId /
// scanIndex, http(s)-only urls) is the registry's ask schema; only the preview actions
// need this module, since they are ordinary §2 actions.
export const validateAsk = (ask, warnings) => {
  if (ask == null) return null;
  SCHEMA.validateAsk(ask);
  const card = SCHEMA.normalizeAsk(ask);
  return {
    question: card.question,
    mode: card.mode,
    allowCustom: card.allowCustom === true,
    customLabel: card.customLabel || DEFAULT_CUSTOM_LABEL,
    options: ask.options.map((opt, i) => {
      const where = `ask option ${i + 1}`;
      const out = { label: card.options[i].label };
      // Preview actions are RENDERED never executed, so editor-settings ops are treated as
      // "inside a variant" (rejected). §1 leniency: a misplaced op costs this option its
      // PICTURE, not the plan — §11.2 already renders pictureless options.
      if (opt.actions != null) {
        try {
          out.actions = validateActions(opt.actions, warnings, where, 'variants or previews');
        } catch (err) {
          if (!(err instanceof MisplacedOpError)) throw err;
          warnings.push(`Dropped the preview for ${where} ("${out.label}") — ${err.reason}; the option is still offered`);
        }
      }
      // The client resolves the reference (or renders the option pictureless); nothing
      // here fetches anything.
      if (opt.image != null) out.image = card.options[i].image;
      return out;
    }),
  };
};

// The text an answered card sends as the user's next turn: the picked labels joined, or the
// typed custom text. Trimmed and capped so a pasted essay can't ride back as one "answer".
export const askAnswerText = (ask, { picked = [], custom = '' } = {}) => {
  const typed = String(custom || '').trim();
  if (typed) return typed.slice(0, ASK_LIMITS.answer);
  const labels = (Array.isArray(picked) ? picked : [picked])
    .map((p) => (isObj(p) ? p.label : p)).filter((l) => isStr(l) && l.trim());
  return labels.join(', ').slice(0, ASK_LIMITS.answer);
};

// Raw LLM reply → validated plan { reply, actions, variants, warnings, chatOnly }.
// §1 extraction tolerance: fences stripped, first balanced JSON object wins; no JSON
// object at all = a chat-only turn (raw text = reply — not an error). Invalid plans THROW.
export const parseOpPlan = (text) => {
  const raw = String(text == null ? '' : text);
  const chatOnly = () => ({ reply: raw.trim(), actions: [], variants: [], ask: null, warnings: [], chatOnly: true });
  const candidate = firstJsonObject(raw.replace(/```[a-zA-Z]*/g, ''));
  if (candidate == null) return chatOnly();
  let obj;
  try { obj = JSON.parse(candidate); } catch { return chatOnly(); }   // not actually JSON → chat-only

  // `version` other than 1 (or absent) is accepted but ignored.
  const warnings = [];
  // §1 reply tolerance: models routinely omit the reply while planning valid
  // actions — substitute rather than lose the plan to a missing pleasantry.
  const replyOmitted = typeof obj.reply !== 'string' || !obj.reply.trim();
  let reply = replyOmitted ? '' : obj.reply;
  const actions = validateActions(obj.actions, warnings, '"actions"');
  if (obj.variants != null && !Array.isArray(obj.variants)) throw new Error('Invalid plan: "variants" must be an array');
  const rawVariants = obj.variants || [];
  if (rawVariants.length > LIMITS.variants) throw new Error(`Invalid plan: more than ${LIMITS.variants} variants`);
  // §1 leniency: a variant holding a top-level-only/settings op is DROPPED with a
  // warning naming it — the top-level actions and the well-formed variants still run.
  const variants = [];
  rawVariants.forEach((v, i) => {
    if (!isObj(v)) throw new Error('Invalid plan: every variant must be an object');
    if (v.label != null && !isStr(v.label)) throw new Error('Invalid plan: variant "label" must be a string');
    const label = v.label || `variant ${i + 1}`;
    try {
      variants.push({ label, actions: validateActions(v.actions, warnings, `variant ${i + 1}`, 'variants') });
    } catch (err) {
      if (!(err instanceof MisplacedOpError)) throw err;
      warnings.push(`Dropped variant ${i + 1} ("${label}") — ${err.reason}; the rest of the plan ran`);
    }
  });
  const ask = validateAsk(obj.ask, warnings);
  // The substitute must not overstate what happened: "Done." only when the plan
  // actually carries work — an empty plan says so, since a bare "Done." there
  // reads as a success that never occurred.
  if (replyOmitted) {
    if (actions.length || variants.length || ask) {
      reply = 'Done.';
      warnings.push('The model omitted its reply — the plan still ran');
    } else {
      reply = 'The model returned an empty plan — nothing was changed.';
    }
  }
  return { reply, actions, variants, ask, warnings, chatOnly: false };
};

// Variant labels name files/projects — keep them short and filesystem-safe.
export const sanitizeLabel = (label) => String(label == null ? '' : label)
  .trim().replace(/[^\w \-]+/g, '').replace(/\s+/g, ' ').slice(0, 40).trim() || 'variant';

// §10 connect/disconnect: resolve `server` against a list the USER owns — exact URL
// match, else a UNIQUE host match; anything else fails the plan. The model can NEVER
// introduce a new host, and plans never carry tokens. The matched ENTRY is returned
// (so a saved server's stored token rides along).
export const resolveServer = (server, entries, what) => {
  const want = String(server ?? '').trim();
  const urlOf = (e) => (typeof e === 'string' ? e : e.url);
  const exact = entries.find((e) => urlOf(e) === want);
  if (exact != null) return exact;
  const w = want.toLowerCase();
  const matches = entries.filter((e) => {
    try {
      const u = new URL(urlOf(e));
      return u.host.toLowerCase() === w || u.hostname.toLowerCase() === w;
    } catch { return false; }
  });
  if (matches.length === 1) return matches[0];
  throw new Error(matches.length
    ? `Unknown server "${server}" — that host matches several ${what}; use the full URL`
    : `Unknown server "${server}" — not among your ${what}`);
};

// ── Sandboxing variants / ask previews ──────────────────────────────────────
// Both run their ops against the LIVE editor and then put it back. `load()` restores
// pixels ONLY — it leaves the settings a variant-legal op touched (filter, page,
// formulas) and it CLEARS the user's lines — so the state has to be captured too.
// The layout line fields the registry declares — what a preview snapshot copies back.
const LINE_FIELDS = Object.keys(SCHEMA.ops.get('layout').keys.lines.items.fields);

const captureEditorState = (stencil) => {
  return {
    filter: stencil.filter,
    filterColor: stencil.filterColor,
    pageSize: stencil.pageSize,
    allowFormulas: stencil.allowFormulas,
    formulaX: stencil.formulaX,
    formulaY: stencil.formulaY,
    // Read from `lines` (the LIVE list), not `layout` — that getter is the persisted
    // project layout and goes stale. Copied to plain data: the facade hands back proxies
    // into app.lines, which dereference to nothing once load() clears them.
    lines: (stencil.lines || []).map((l) => {
      const out = { points: (l.points || []).map((p) => ({ x: p.x, y: p.y })) };
      for (const k of LINE_FIELDS) if (k !== 'points' && l[k] != null) out[k] = l[k];
      return out;
    }),
    size: stencil.imageSize,
  };
};

const nextFrame = () => new Promise((r) => (typeof requestAnimationFrame === 'function' ? requestAnimationFrame(r) : setTimeout(r, 16)));

const lineCount = (stencil) => (stencil.lines || []).length;

// Silent install: no replace prompt, no "pasted" toast, and out of undo — this is
// putting the user's OWN lines back after a sandboxed run, not a new edit.
const applyLines = (stencil, s) => { stencil.setLines(s.lines, { history: false }); };

const restoreEditorState = async (stencil, s, reloaded) => {
  stencil.apply({
    filter: s.filter, filterColor: s.filterColor, pageSize: s.pageSize,
    allowFormulas: s.allowFormulas, formulaX: s.formulaX, formulaY: s.formulaY,
  });
  // Lines are only touched when the ops disturbed them; otherwise they are already
  // right and writing them again would be pointless UI noise.
  if (!reloaded) return;
  // load() clears the line list a few frames AFTER it resolves. Wait for that, because
  // installing lines while some still exist raises the editor's "Replace layout?"
  // prompt — this is an internal restore, never a user paste.
  for (let i = 0; i < 40 && lineCount(stencil) !== 0; i++) await nextFrame();
  if (!s.lines.length) return;
  applyLines(stencil, s);
  // Hold it across the rest of the settle window, re-asserting only from empty.
  for (let i = 0, stable = 0; i < 40 && stable < 8; i++) {
    if (lineCount(stencil) === s.lines.length) stable++;
    else { if (lineCount(stencil) === 0) applyLines(stencil, s); stable = 0; }
    await nextFrame();
  }
};

// exportImage() renders the filter AND the lines into the pixels, so a snapshot taken
// for RESTORING must have both switched off — reloading a normal export would bake the
// filter in permanently and burn the annotations into the image.
const capturePixels = async (stencil, exportImage) => {
  const { filter, showPoints, showLines } = stencil;
  stencil.apply({ filter: 'none', showPoints: false, showLines: false });
  try {
    return await exportImage();
  } finally {
    stencil.apply({ filter, showPoints, showLines });
  }
};

// Ops whose effect a settings restore alone cannot undo — the pixels, or the line list.
// A variant built only from the others (filter, page, formula) needs no reload at all,
// which keeps the common case fast and leaves the user's lines untouched.
const NEEDS_RELOAD = new Set(['crop', 'rotate', 'blank', 'frame', 'layout']);

// Put the editor back exactly as `state`/`pixels` found it. The reload is skipped when
// the ops that ran could not have touched the pixels.
const restoreWorkingImage = async (stencil, pixels, state, actions) => {
  const reload = (actions || []).some((a) => NEEDS_RELOAD.has(a.op));
  if (reload) await stencil.load(pixels);
  await restoreEditorState(stencil, state, reload);
};

// Execute a parsed plan against the frozen window.stencil facade — every op routes
// through the same facade methods the toolbar/console use, never new editor logic.
// The options are the surface's injected capabilities (chatSession.js wires them all);
// an absent one makes its op error out or note+skip per §10/§2.1. savedServers is the
// ONLY pool `connect` may resolve against (§10; plans never carry tokens).
// Returns { results: [{ label, dataUrl }], warnings }.
export const executeOpPlan = async (plan, stencil, { exportImage, loadFrame, savedServers, userText, openIncognito, loadAttachment, saveProject, copyRendered, copyLayoutRendered, removeProjectNamed, clearWorkingImage, clearLocalProjects, renameActiveProject, setBlankColor, openProjectNamed, clearChatConversation, setChatPlacement, openDialog, setVoiceChat, deferredSink } = {}) => {
  const warnings = (plan.warnings || []).slice();
  const results = [];

  // Dispatch through the registry — the parser only emits ops it holds. `notes` lets an
  // executor report what IT did (rendered with the reply); `frame` is the §1 re-mapping
  // accumulated from executed crops/rotates, reset to identity by newFrame ops.
  const ctx = { stencil, exportImage, loadFrame, savedServers, userText, openIncognito, loadAttachment, saveProject, copyRendered, copyLayoutRendered, removeProjectNamed, clearWorkingImage, clearLocalProjects, renameActiveProject, setBlankColor, openProjectNamed, clearChatConversation, setChatPlacement, openDialog, setVoiceChat, results, notes: warnings, frame: identityFrame() };
  const run = async (a) => {
    // §13: a forbidden op is refused with a typed error even if a plan carrying
    // one reached the executor without passing the parser's unknown-op drop.
    if (FORBIDDEN_OPS.has(a.op)) throw new ForbiddenOpError(a.op);
    await OPS[a.op].run(a, ctx);
    if (OPS[a.op].newFrame) Object.assign(ctx.frame, identityFrame());
  };

  // §10 clearChat defers to the plan's END, wherever it rode in the plan. A caller
  // `deferredSink` takes the deferred actions UNEXECUTED instead: the turn runner replays
  // them after the §7 auto-continuation round, at the turn's true end (chatController).
  const deferred = deferredSink || [];
  for (const a of plan.actions) {
    if (OPS[a.op]?.deferred) deferred.push(a);
    else await run(a);
  }

  if (plan.variants.length) {
    if (!exportImage) throw new Error('Variants need an image export capability');
    // Variants render IMAGES, so they need a working image — but a plan whose
    // actions already ran (settings, openUrl, blank…) must not be thrown away
    // for that: skip the renders with a warning instead of failing the turn.
    if (!stencil.imageSize) {
      warnings.push(`Skipped ${plan.variants.length} variant${plan.variants.length === 1 ? '' : 's'} — variants render images and no image is loaded yet`);
    } else {
      // Branch each variant from the state AFTER the top-level actions: snapshot the
      // working image + editor state, apply the variant's ops, export, then restore both.
      const state = captureEditorState(stencil);
      const base = await capturePixels(stencil, exportImage);
      // Each variant branches from the post-actions state, so its coordinate
      // re-mapping restarts from the transform the top-level actions built up.
      const postActionsFrame = { ...ctx.frame };
      for (const v of plan.variants) {
        try {
          Object.assign(ctx.frame, postActionsFrame);
          for (const a of v.actions) await run(a);
          results.push({ label: sanitizeLabel(v.label), dataUrl: await exportImage() });
        } finally {
          await restoreWorkingImage(stencil, base, state, v.actions);
        }
      }
    }
  }

  if (!deferredSink) for (const a of deferred) await run(a);

  return { results, warnings };
};

// ── §11 preview rendering ───────────────────────────────────────────────────
// Render the preview for each `ask` option carrying `actions` — same save/restore dance
// as variants, so a preview SUGGESTS an edit, never performs one. Options with an `image`
// reference (or nothing) come back without a dataUrl for the surface to resolve. One
// option's render failing costs that option its picture (+ warning), never the card.
export const renderAskPreviews = async (ask, stencil, { exportImage, loadFrame } = {}) => {
  const previews = [];
  const warnings = [];
  const renderable = (ask?.options || []).map((o, i) => [o, i]).filter(([o]) => o.actions?.length);
  if (!renderable.length || typeof exportImage !== 'function') return { previews, warnings };
  // A preview is a RENDER of the working image — without one the card still
  // stands (labels only, §11.2), so this must never fail the turn.
  if (!stencil.imageSize) {
    warnings.push('Option previews need a working image — the choices are shown without pictures');
    return { previews, warnings };
  }
  const state = captureEditorState(stencil);
  const base = await capturePixels(stencil, exportImage);
  for (const [opt, index] of renderable) {
    try {
      await executeOpPlan({ actions: opt.actions, variants: [], warnings: [] }, stencil, { exportImage, loadFrame });
      previews.push({ index, label: opt.label, dataUrl: await exportImage() });
    } catch (err) {
      warnings.push(`Could not preview "${opt.label}" — ${err?.message || err}`);
    } finally {
      // Whatever happened, the working image AND the editor state go back.
      await restoreWorkingImage(stencil, base, state, opt.actions);
    }
  }
  return { previews, warnings };
};
