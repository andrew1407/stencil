// ── Op-plan: the executor + this module's public surface (llm-contract.md §1–4) ──
// Pure — no DOM, no fetch. The LLM never touches pixels: it emits a strictly validated plan
// of whitelisted ops that executeOpPlan maps 1:1 onto the same window.stencil facade calls
// the toolbar uses. LLM output is data, not instructions.
import { identityFrame } from './frame.js';
import { OPS } from './opExecutors.js';
import { FORBIDDEN_OPS, ForbiddenOpError } from './promptAssembly.js';
import { sanitizeLabel } from './planValues.js';
import { captureEditorState, capturePixels, restoreWorkingImage, needsPixelSnapshot } from './planSandbox.js';

export { PROMPT_CORE_HEAD, PROMPT_CORE_TAIL, SCHEMA, LIMITS, ASK_LIMITS, DEFAULT_CUSTOM_LABEL } from './planSchema.js';
export { sanitizeLabel, resolveServer } from './planValues.js';
export { OPS } from './opExecutors.js';
export {
  BROWSER_CAPABILITIES, FORBIDDEN_OPS, ForbiddenOpError, assemblePrompts,
  LLM_SYSTEM_PROMPT, EDITOR_SETTINGS_PROMPT, EDITOR_SYSTEM_PROMPT,
} from './promptAssembly.js';
export { MisplacedOpError, validateAsk, askAnswerText, parseOpPlan } from './planParser.js';

// Execute a parsed plan against the frozen window.stencil facade — every op routes
// through the same facade methods the toolbar/console use, never new editor logic.
// The options are the surface's injected capabilities (chatSession.js wires them all);
// an absent one makes its op error out or note+skip per §10/§2.1. savedServers is the
// ONLY pool `connect` may resolve against. Returns { results: [{ label, dataUrl }], warnings }.
export const executeOpPlan = async (plan, stencil, { exportImage, loadFrame, savedServers, userText, openIncognito, loadAttachment, saveProject, copyRendered, copyLayoutRendered, removeProjectNamed, clearWorkingImage, clearLocalProjects, renameActiveProject, setBlankColor, openProjectNamed, clearChatConversation, setChatPlacement, openDialog, setVoiceChat, deferredSink, ranSink } = {}) => {
  const warnings = (plan.warnings || []).slice();
  const results = [];

  // `notes`: what an executor reports (rendered with the reply); `frame`: the §1 re-mapping
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
  // `ranSink` collects the actions that completed — what a preview sandbox must undo.
  for (const a of plan.actions) {
    if (OPS[a.op]?.deferred) deferred.push(a);
    else { await run(a); ranSink?.push(a); }
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
      // editor state (pixels only for a variant that replaces the original), run the
      // variant's ops from the frame the top-level actions built up, export, restore.
      const state = captureEditorState(stencil);
      const postActionsFrame = { ...ctx.frame };
      let base = null;
      for (const v of plan.variants) {
        const ran = [];
        try {
          if (needsPixelSnapshot(v.actions)) base ??= await capturePixels(stencil, exportImage);
          Object.assign(ctx.frame, postActionsFrame);
          for (const a of v.actions) { await run(a); ran.push(a); }
          results.push({ label: sanitizeLabel(v.label), dataUrl: await exportImage() });
        } finally {
          await restoreWorkingImage(stencil, base, state, ran);
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
  let base = null;
  for (const [opt, index] of renderable) {
    const ran = [];   // whatever happened, the working image AND the editor state go back
    try {
      if (needsPixelSnapshot(opt.actions)) base ??= await capturePixels(stencil, exportImage);
      await executeOpPlan({ actions: opt.actions, variants: [], warnings: [] }, stencil, { exportImage, loadFrame, ranSink: ran });
      previews.push({ index, label: opt.label, dataUrl: await exportImage() });
    } catch (err) {
      warnings.push(`Could not preview "${opt.label}" — ${err?.message || err}`);
    } finally {
      await restoreWorkingImage(stencil, base, state, ran);
    }
  }
  return { previews, warnings };
};
