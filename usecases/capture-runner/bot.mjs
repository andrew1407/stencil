// Captures usecases/docs/bot/img/* from the real Telegram Web client talking to the bot. Semi-
// manual by design: Telegram needs a person to log in once, in a dedicated profile kept
// outside the repo, and the bot must be running (the live one, or `dotnet run` locally with
// this account on its allowlist). This script never starts the bot and never reads its env.
//   node usecases/capture-runner/bot.mjs [--bot stencil_editor_bot] [--profile ~/.stencil-docs-telegram]
import { chromium } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { expandHome, outDir, repoPath } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shotRunner.mjs';
import { settle } from './lib/waits.mjs';

const config = loadCaptureConfig('bot');
const runner = makeShotRunner({ config, out: outDir('bot') });
const WAITS = config.get('waits');
const ICON = repoPath(config.get('urls.localBotIcon'));
const arg = (flag, fallback) => {
  const at = process.argv.indexOf(flag);
  return at >= 0 ? process.argv[at + 1] : fallback;
};
const BOT = arg('--bot', config.get('bot')).replace(/^@/, '');
const PROFILE = expandHome(arg('--profile', config.get('profileDir')));

// Telegram's own dark theme follows the emulated colour scheme.
const context = await chromium.launchPersistentContext(PROFILE, {
  headless: false, viewport: config.get('viewport'), colorScheme: runner.themeOf('chat'),
});
const page = context.pages()[0] || await context.newPage();

// Telegram Web "A" selectors, in one place: the client's DOM changes without notice.
const SEL = Object.freeze({
  auth: '#auth-qr-form, .auth-form, #auth-phone-number-form',
  search: '#telegram-search-input',
  // The list is virtualised: off-screen rows exist but cannot be clicked, so match a visible one.
  result: (bot) => page.locator('.ListItem.chat-item-clickable:visible').filter({ hasText: new RegExp(bot, 'i') }),
  startButton: '#MiddleColumn button:has-text("START")',
  header: '.MiddleHeader',
  footer: '.middle-column-footer',
  input: '#editable-message-text',
  messages: '.MessageList .Message',
  list: '#MiddleColumn .MessageList',
  inlineButton: '.Message .message-content-wrapper button',
  attach: '.AttachMenu button, button[aria-label="Add an attachment"]',
  attachPhoto: '.MenuItem:has-text("Photo")',
  attachSend: '.modal-dialog button:has-text("Send"), .Modal button:has-text("Send")',
});

await page.goto('https://web.telegram.org/a/');
await page.locator(`${SEL.auth}, ${SEL.search}`).first().waitFor({ timeout: 60_000 });
// No login yet: the window stays open for a person to log in (QR or phone); polled, so this
// works from a background run as well as a terminal.
for (let waited = 0; await page.locator(SEL.auth).first().isVisible().catch(() => false); waited += WAITS.loginPollMs) {
  if (waited % 60_000 === 0) console.log(`waiting for a Telegram login in the open window (profile ${PROFILE})…`);
  if (waited > WAITS.loginMaxMs) throw new Error('no Telegram login after 20 minutes');
  await settle(WAITS.loginPollMs);
}
// Open the bot's chat through the search box: the address-bar routes land on whatever chat
// was open last.
await page.locator(SEL.search).click();
await page.keyboard.type(BOT, { delay: 40 });   // a React-controlled input ignores fill()
await SEL.result(BOT).first().waitFor({ timeout: WAITS.menuMs });
await SEL.result(BOT).first().click({ timeout: WAITS.menuMs });
await page.locator(`${SEL.input}, ${SEL.startButton}`).first().waitFor({ timeout: WAITS.menuMs });

// An answer is not always a NEW message — an edit menu rewrites its keyboard in place. This
// stamps the INCOMING side only, so the typed line never counts as the answer.
const replyStamp = () => page.evaluate((sel) => {
  const all = [...document.querySelectorAll(sel)].filter((el) => !el.classList.contains('own'));
  const last = all[all.length - 1];
  return [all.length, last?.getAttribute('data-message-id') ?? '',
    last?.querySelector('img')?.src ?? '', (last?.innerText ?? '').slice(0, 120)].join('|');
}, SEL.messages);

// The same reading, in the page, compared against the one taken before the send. Written out
// rather than passed as a string: the client's CSP forbids eval.
const waitForReply = async (before, timeout = WAITS.replyMs) => {
  await page.waitForFunction(([sel, was]) => {
    const all = [...document.querySelectorAll(sel)].filter((el) => !el.classList.contains('own'));
    const last = all[all.length - 1];
    return [all.length, last?.getAttribute('data-message-id') ?? '',
      last?.querySelector('img')?.src ?? '', (last?.innerText ?? '').slice(0, 120)].join('|') !== was;
  }, [SEL.messages, before], { timeout });
  await settle(WAITS.settleMs);   // the photo and its keyboard land a beat after the message
};
// A chat never opened before shows START instead of the composer; pressing it is /start.
let started = false;
if (await page.locator(SEL.startButton).isVisible().catch(() => false)) {
  const before = await replyStamp();
  await page.locator(SEL.startButton).click();
  await waitForReply(before);
  started = true;
}
await page.locator(SEL.input).waitFor({ timeout: WAITS.menuMs });

const send = async (text, timeout = WAITS.replyMs) => {
  await page.locator(SEL.input).click();
  await page.keyboard.type(text);
  const before = await replyStamp();
  await page.keyboard.press('Enter');
  await waitForReply(before, timeout);
};
// A submenu button edits the keyboard in place (wait for `reveals`); an action sends a reply.
const tap = async (label, reveals) => {
  const before = await replyStamp();
  await page.locator(SEL.inlineButton).filter({ hasText: label }).last().click();
  if (reveals) await page.locator(SEL.inlineButton).filter({ hasText: reveals }).last().waitFor({ timeout: WAITS.menuMs });
  else await waitForReply(before);
};
const upload = async (file) => {
  const before = await replyStamp();
  await page.locator(SEL.attach).first().click();
  const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.locator(SEL.attachPhoto).first().click()]);
  await chooser.setFiles(file);
  const sendBtn = page.locator(SEL.attachSend).first();
  await sendBtn.waitFor({ timeout: WAITS.menuMs }).catch(() => {});
  if (await sendBtn.isVisible().catch(() => false)) await sendBtn.click(); else await page.keyboard.press('Enter');
  await waitForReply(before);
};
// Telegram animates every arrival and floats a date pill over the column, so both are stilled
// before any shot. An inline keyboard button is rgba(72,87,97,.4) — the wallpaper reads through.
const STILL_CSS = `
  .sticky-date, .ripple-container { display: none !important; }
  *, *::before, *::after { animation: none !important; transition: none !important; }
  .Message, .message-content-wrapper, .bubble { opacity: 1 !important; }
  .Message .message-content-wrapper button.Button {
    background-color: rgb(45, 53, 60) !important;
    backdrop-filter: none !important;
  }
  .Message .message-content-wrapper button .emoji { margin-right: 5px !important; }
`;
const stillChat = () => page.addStyleTag({ content: STILL_CSS }).catch(() => {});

// The conversation alone: the column between the header and the composer, cut at a message
// boundary — a clip that starts mid-keyboard reads as broken chrome.
const shot = async (name, keep = 2) => {
  await page.mouse.wheel(0, 4000);
  await settle(WAITS.settleMs);
  await stillChat();
  const column = await page.locator(SEL.list).boundingBox();
  const head = await page.locator(SEL.header).boundingBox();
  const foot = await page.locator(SEL.footer).boundingBox();
  const floor = head.y + head.height + 8;
  const tops = await page.locator(SEL.messages).evaluateAll(
    (els, n) => els.slice(-n).map((el) => el.getBoundingClientRect().top), keep);
  const whole = tops.filter((top) => top >= floor);
  const top = Math.max(floor, whole.length ? Math.min(...whole) - 6 : floor);
  await runner.shot(page, name, { clip: { x: column.x, y: top, width: column.width, height: foot.y - top } });
};
// The progress notice comes first, the rendered photo when the plan has run.
const waitForPlan = () => page.locator(SEL.messages).filter({ hasText: /Working on your request/ })
  .last().waitFor({ state: 'detached', timeout: WAITS.promptMs }).catch(() => {});
const ensurePhoto = async (ctx) => {
  if (ctx.photo) return;
  await upload(ICON);
  ctx.photo = true;
};

const STEPS = Object.freeze([
  { name: 'start-menu', run: async () => { if (!started) await send('/start'); await shot('start-menu'); } },
  { name: 'photo-edit-menu', run: async (ctx) => { await ensurePhoto(ctx); await shot('photo-edit-menu'); } },
  { name: 'crop', run: async () => { await send('/crop x1=10% x2=90% y1=10% y2=90%'); await shot('crop'); } },
  { name: 'filter', run: async () => {
    await tap('Filter', 'Sepia');
    await shot('filter-menu');
    await tap('B&W');
    await shot('filter-bw');
  } },
  // Needs the bot started with STENCIL_LLM_* pointing at a model.
  { name: 'prompt', run: async (ctx) => {
    await ensurePhoto(ctx);
    await send(`/prompt ${config.prompt('real')}`, WAITS.promptMs);
    await waitForPlan();
    await shot('prompt');
  } },
  { name: 'draw', run: async () => { await send('/draw rect 20%,20% 80%,80%'); await shot('draw-rect'); } },
  // The reply keyboards behind the edit menu's buttons; each submenu edits it in place.
  { name: 'menus', run: async (ctx) => {
    await ensurePhoto(ctx);
    for (const [name, button, reveals] of [['menu-edit', 'Edit', 'Crop'], ['menu-draw', 'Draw', 'Draw…'],
      ['menu-download', 'Download', 'Image']]) {
      await tap(button, reveals);
      await shot(name);
      await tap('Back', 'Filter');
    }
  } },
  // Chat mode: a plain message is a prompt, until "Chat off".
  { name: 'chat', run: async (ctx) => {
    await ensurePhoto(ctx);
    await tap('Chat');
    await shot('chat-mode');
    await send(config.prompt('botChat'));
    await waitForPlan();
    await shot('chat-turn');
    await tap('Chat off');
  } },
  { name: 'script', run: async () => {
    await send('/script @crop 10% ; @filter sepia ; @line (0,0) (100%,100%)');
    await shot('script');
  } },
  { name: 'status', run: async () => { await send('/status'); await shot('status'); } },
  { name: 'blank', run: async () => { await send('/blank b5 black'); await shot('blank'); } },
  { name: 'help', run: async () => { await send('/help'); await shot('help'); } },
]);

console.log('bot chat');
await runner.play(STEPS, { photo: false });
await context.close();
runner.finish();
