// The CLI stage: the Zig console over the whole page — the pinned banner, the status line, the
// scrollback and a prompt — on the app's theme. A view over the live editor: its commands are the
// console's, through the facade, so every edit is real and stays when the stage leaves.
import FX from '../../../../common/config/console/fx.json' with { type: 'json' };
import { subscribe, EVENTS } from '../../eventBus/appBus.js';
import { FLUSH } from '../../core/app/changes.js';
import { accentHex, DEFAULT_ACCENT } from '../../core/settings/accents.js';
import { createInterpreter } from '../../console/shell/interpreter.js';
import { CLI_MODE } from '../../console/shell/table.js';
import { wireLogoHold } from '../logo/stageTrigger.js';
import { createConsoleView } from '../console/view.js';
import { wireConsoleInput } from '../console/input.js';
import { paintBanner } from './banner.js';
import { paintStatus, wireStatusGestures } from './status.js';
import { createCliPeek } from './peek.js';
import { cliCommands, secretWord } from './commands.js';
import { cycleAccent, randomVividAccent } from './accent.js';
import { createRecolour } from './recolour.js';
import { wearSkin } from './skin/paint.js';
import { traitsOf, skinOf, LIST_WORD } from './skin/table.js';
import { accentUnder, fillRun, rowKey } from './skin/restyle.js';

export const STAGE_CLASS = 'cli-stage';
// The Zig console's own intro (uiText.txt @intro-fullscreen), less its clipboard clause.
const INTRO = "Console mode — '/command <args>' (the '/' is optional). Tab completes, Up/Down recall history. '/help' lists commands, '/exit' leaves.";
const CLICK_MS = 250;   // the double-click window a single click waits out (line_edit/read.zig)

const build = (doc) => {
  const make = (tag, cls, attrs = {}) => {
    const el = doc.createElement(tag);
    el.className = cls;
    for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
    return el;
  };
  const host = make('div', STAGE_CLASS, { role: 'application' });
  const header = make('div', 'cli-header');
  const banner = make('pre', 'cli-banner', { 'aria-hidden': 'true' });
  const status = make('div', 'cli-status');
  const peek = make('div', 'cli-peek');
  peek.hidden = true;
  header.append(banner, status, peek);
  const out = make('pre', 'cli-out', { 'aria-live': 'polite' });
  // The two accent rules of the Zig screen: above the prompt and along the bottom row.
  const rule = make('div', 'cli-rule');
  const ruleEnd = make('div', 'cli-rule cli-rule-end');
  const promptRow = make('div', 'cli-prompt-row');
  const prompt = make('span', 'cli-prompt', { 'aria-hidden': 'true' });
  prompt.textContent = '>';
  const input = make('input', 'cli-input', { type: 'text', autocomplete: 'off', spellcheck: 'false', 'aria-label': 'CLI command' });
  // A browser clips no gradient to an input's text, so under the rainbow a mirror over it carries the hues.
  const mirror = make('span', 'cli-input-mirror', { 'aria-hidden': 'true' });
  promptRow.append(prompt, input);
  host.append(header, out, rule, promptRow, ruleEnd);
  return { host, banner, status, peek, rules: [rule, ruleEnd], prompt, out, input, mirror, promptRow };
};

export const openCliStage = (app, { doc = globalThis.document, onLeave = () => {}, schedule = (fn, ms) => setTimeout(fn, ms) } = {}) => {
  const { host, banner, status, peek: peekSlot, rules, prompt, out, input, mirror, promptRow } = build(doc);
  doc.body.appendChild(host);
  const view = createConsoleView(out);
  const accentNow = () => app.customAccent || accentHex(app.accent || DEFAULT_ACCENT);
  let skin = null, clock = null, frame = 0, worn = { off: () => {}, cut: () => {} }, ownAccent = null, released = false;
  // The fairy lights borrow the accent; the one the user had, preset or custom, comes back after.
  const restoreAccent = () => {
    const own = ownAccent;
    ownAccent = null;
    if (!own) return;
    if (own.custom) app.accents.setCustomAccent(own.custom); else app.accents.setAccent(own.key);
  };

  // The scrollback's accent runs re-dressed in the worn skin, as the Zig console restyles every
  // row it paints; the mirror shows the typed line over the input, its word in the input's colour.
  const dress = (el, text, full) => fillRun(el, text, { skin, clock, key: rowKey(full) });
  view.setDecorator(dress);
  const redress = () => { for (const el of view.runs()) dress(el, el.dataset.shown ?? '', el.dataset.text ?? el.parentNode?.dataset?.text ?? ''); };
  let mirrored = false;
  const mirrorInput = () => {
    if (!mirrored) return;
    const v = input.value, end = v.search(/\s/), cut = end < 0 ? v.length : end;
    while (mirror.firstChild) mirror.removeChild(mirror.firstChild);
    const word = doc.createElement('span');
    word.className = 'cli-input-word';
    word.textContent = v.slice(0, cut);
    mirror.append(word, doc.createTextNode(v.slice(cut)));
    mirror.dataset.word = input.dataset.word ?? '';
    mirror.style.left = `${input.offsetLeft}px`;
  };
  const paintWith = (accent) => { paintBanner(banner, { skin, accent: accentUnder(skin, accent), clock, frame }); redress(); };
  const repaint = (f = frame, c = clock) => { frame = f; clock = c; paintWith(accentNow()); };
  const recolour = createRecolour({ host, banner, out, prompt, rules, skin: () => skin, paint: paintWith, schedule });
  recolour.setPainted(accentNow());
  const peek = createCliPeek(app, peekSlot);
  const paintStatusLine = () => paintStatus(status, app);
  const refreshHeader = () => { paintStatusLine(); peek.refresh(); };
  const stage = {
    leave: () => onLeave(),
    clear: () => view.clear(),
    printSecret: (lines) => view.print(lines, 'secret'),
    skin: () => skin,
    wearSkin(name) {
      recolour.cancel();
      worn.off();
      if (skin === 'fairylight') restoreAccent();
      skin = name;
      const hues = !!traitsOf(name)?.paintsRuns;
      if (hues !== mirrored) { if (hues) promptRow.appendChild(mirror); else promptRow.removeChild(mirror); }
      mirrored = hues;
      mirrorInput();
      if (name === 'fairylight') ownAccent = { key: app.accent, custom: app.customAccent || null };
      worn = name ? wearSkin(host, name, {
        repaint, out,
        print: (lines, kind) => view.print(lines, kind), setAccent: (hex) => app.accents.setCustomAccent(hex),
      }) : (repaint(0, null), { off: () => {}, cut: () => {} });
    },
  };
  const interpreter = createInterpreter({
    app,
    extra: cliCommands({ app, stage }),
    host: {
      print: (lines, kind) => view.print(lines, kind),
      clear: () => view.clear(),
      close: () => onLeave(),
      pickFile: () => view.print(['error: no file chooser in CLI mode — give a URL, or /clioff and use the Open Image window'], 'error'),
      cliMode: () => true,
      secret: (word) => secretWord(word, stage),
      isSecret: (word) => word === LIST_WORD || !!skinOf(word),
      skinOff: () => { if (skin) stage.wearSkin(null); },
      statusChanged: refreshHeader,
    },
  });
  wireConsoleInput(input, interpreter);
  for (const ev of ['input', 'keyup']) input.addEventListener(ev, mirrorInput);   // after the input's own tint, which names the word

  // Every new accent — a /theme, a logo click, a fairy-light bulb — wipes over the banner, as the
  // Zig console's onThemeChanged does; the compact press frame belongs to the logo press alone.
  const onAccent = () => { if (!released && accentNow() !== recolour.painted()) recolour.wipeTo(accentNow()); };
  let pressed = false, pressFrameDue = false;
  const restoreFrame = () => { if (!released && !recolour.wiping()) repaint(); };   // a wipe already running keeps its frame
  const pressFrame = () => {
    if (released) return;
    recolour.cancel();   // a press is queued input: a running wipe settles first (timing.zig)
    paintBanner(banner, { skin, accent: accentUnder(skin, recolour.painted()), compact: true, clock, frame });
    pressFrameDue = true;
    // The press frame's time (press.zig), and then as long as the button stays down.
    schedule(() => { pressFrameDue = false; if (!pressed) restoreFrame(); }, FX.press.ms);
  };
  const letGo = () => { pressed = false; if (!pressFrameDue) restoreFrame(); };
  // A queued keystroke settles whatever is playing: the wipe, the reveal, a skin's entrance.
  const settleEffects = () => { recolour.cancel(); view.settleNow(); worn.cut(); };

  const offs = [
    subscribe(EVENTS.accentChanged, onAccent),
    subscribe(EVENTS.themeChanged, () => repaint()),
    app.changes?.on?.(FLUSH, refreshHeader) ?? (() => {}),
  ];
  const gestures = wireStatusGestures(status, peek);
  // The logo is the Zig console's (hooks.zig): the press shows the press frame; a click, once the
  // double-click window has passed, takes a worn skin off or else advances the accent; a
  // double-click lands on a random vivid colour; the long press leaves. No picker, no page menu.
  let clickTimer = null;
  const plain = (e) => !(e.altKey || e.ctrlKey || e.metaKey || e.shiftKey || e.button);
  banner.addEventListener('pointerdown', (e) => { if (plain(e)) { pressed = true; pressFrame(); } });
  banner.addEventListener('pointerleave', letGo);
  banner.addEventListener('pointerup', (e) => {
    if (!plain(e) || !pressed || released) return;
    letGo();
    if (clickTimer) {   // the second click of a double-click
      clearTimeout(clickTimer);
      clickTimer = null;
      if (skin) stage.wearSkin(null); else randomVividAccent(app, banner);
      return;
    }
    clickTimer = schedule(() => {
      clickTimer = null;
      if (skin) stage.wearSkin(null); else cycleAccent(app, banner);
    }, CLICK_MS);
  });
  wireLogoHold(banner, app, { holdMs: CLI_MODE.holdMs, onHold: () => onLeave() });
  // Every key is the prompt's and settles what is playing; nothing reaches the editor's hotkeys,
  // PgUp/PgDn page the scrollback, and Escape leaves nothing.
  const swallow = (e) => {
    settleEffects();
    if (e.key === 'PageUp' || e.key === 'PageDown') { out.scrollBy?.(0, (e.key === 'PageUp' ? -1 : 1) * out.clientHeight); e.preventDefault(); }
    if (e.target === input) return;
    if (e.key !== 'Escape' && !e.ctrlKey && !e.metaKey) input.focus();
    e.stopPropagation();
  };
  doc.addEventListener('keydown', swallow, true);
  host.addEventListener('pointerdown', (e) => { if (!e.target.closest?.('.cli-banner, .cli-status, .cli-out')) input.focus(); });

  repaint(0, null);
  paintStatusLine();
  view.print([INTRO], 'plain');
  schedule(() => input.focus(), 0);

  return {
    host,
    view,
    interpreter,
    skin: () => skin,
    wearSkin: stage.wearSkin,
    release() {
      released = true;
      if (clickTimer) clearTimeout(clickTimer);
      recolour.cancel();
      worn.off();
      for (const off of offs) off();
      restoreAccent();
      doc.removeEventListener('keydown', swallow, true);
      gestures.dispose();
      host.remove();
    },
  };
};
