// Telegram Web as the bot capture drives it: the bot's chat opened behind a person's login,
// a line sent, a reply button tapped on the newest message, a file attached, and the
// conversation column clipped at a message boundary — grown tall enough for what it keeps.
import { settle } from '../lib/waits.mjs';

// Telegram Web "A" selectors, in one place: the client's DOM changes without notice.
const SEL = Object.freeze({
  auth: '#auth-qr-form, .auth-form, #auth-phone-number-form',
  search: '#telegram-search-input',
  result: '.ListItem.search-result:visible',
  startButton: '#MiddleColumn button:has-text("START")',
  header: '.MiddleHeader',
  footer: '.middle-column-footer',
  input: '#editable-message-text',
  messages: '.MessageList .Message',
  list: '#MiddleColumn .MessageList',
  inlineButton: '.message-content-wrapper button',
  attach: '.AttachMenu button, button[aria-label="Add an attachment"]',
  attachMedia: '.MenuItem:has-text("Photo or Video")',
  attachFile: '.MenuItem:text-is("File")',
  attachCaption: '.modal-dialog .form-control, .Modal .form-control',
  attachSend: '.modal-dialog button:has-text("Send"), .Modal button:has-text("Send")',
});

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

// Runs in the page (the client's CSP forbids eval): the newest incoming message id. Ids grow,
// so "newer than" survives the list unloading old messages.
const newestIncoming = (sel) => Math.max(0, ...[...document.querySelectorAll(sel)]
  .filter((el) => !el.classList.contains('own'))
  .map((el) => Number(el.getAttribute('data-message-id')) || 0));

// `expect` incoming messages newer than `since` have landed below the newest own message, and no
// `notice` (a progress line the bot deletes when it is done) is still showing.
const answered = ([sel, since, expect, notice]) => {
  const all = [...document.querySelectorAll(sel)];
  const incoming = all.filter((el) => !el.classList.contains('own'));
  const below = all.slice(all.findLastIndex((el) => el.classList.contains('own')) + 1);
  const fresh = below.filter((el) => Number(el.getAttribute('data-message-id')) > since);
  return fresh.length >= expect && !(notice && incoming.some((el) => new RegExp(notice).test(el.innerText)));
};

// Every picture in the `n` newest messages has loaded: a photo grows from its placeholder.
const pictured = ([sel, n]) => [...document.querySelectorAll(sel)].slice(-n)
  .every((el) => [...el.querySelectorAll('img:not(.emoji)')].every((img) => img.complete));

// The height the `n` newest messages span, from the top of the oldest to the bottom of the newest.
const spanOf = (els, n) => {
  const kept = els.slice(-n);
  return kept.length ? Math.round(kept[kept.length - 1].getBoundingClientRect().bottom - kept[0].getBoundingClientRect().top) : 0;
};

export async function openBotChat(page, { bot, waits, profile }) {
  await page.goto('https://web.telegram.org/a/');
  await page.locator(`${SEL.auth}, ${SEL.search}`).first().waitFor({ timeout: 60_000 });
  // No login yet: the window stays open for a person to log in (QR or phone), polled so this
  // works from a background run as well as a terminal.
  for (let waited = 0; await page.locator(SEL.auth).first().isVisible().catch(() => false); waited += waits.loginPollMs) {
    if (waited % 60_000 === 0) console.log(`waiting for a Telegram login in the open window (profile ${profile})…`);
    if (waited > waits.loginMaxMs) throw new Error('no Telegram login after 20 minutes');
    await settle(waits.loginPollMs);
  }
  // Through the search box: the address-bar routes land on whatever chat was open last. Only a
  // CHAT result counts — a message hit naming the bot (BotFather's) is another conversation.
  await page.locator(SEL.search).click();
  await page.keyboard.type(bot, { delay: 40 });   // a React-controlled input ignores fill()
  await page.locator(SEL.result).first().click({ timeout: waits.menuMs });
  await page.locator(SEL.header).first().waitFor({ timeout: waits.menuMs });
  const headingNow = async () => (await page.locator(SEL.header).first().innerText().catch(() => '')).trim();
  let heading = await headingNow();
  for (let waited = 0; !/\bbot\b/i.test(heading) && waited < waits.menuMs; waited += 500) {
    await settle(500);
    heading = await headingNow();
  }
  if (!/\bbot\b/i.test(heading) || /botfather/i.test(heading)) {
    throw new Error(`the open chat is not @${bot} (header: ${JSON.stringify(heading.slice(0, 80))}); nothing was sent`);
  }
  await page.locator(`${SEL.input}, ${SEL.startButton}`).first().waitFor({ timeout: waits.menuMs });
  return makeChat(page, waits);
}

function makeChat(page, waits) {
  const since = () => page.evaluate(newestIncoming, SEL.messages);
  // `expect`: how many messages the bot answers with; `notice`: its progress line, if any.
  const answer = async (before, { expect = 1, notice = null, timeout = waits.replyMs } = {}) => {
    await page.waitForFunction(answered, [SEL.messages, before, expect, notice?.source ?? null], { timeout });
    await settle(waits.settleMs);   // a photo's keyboard lands a beat after the photo
  };
  // The newest message that carries a keyboard: every tap and every reveal is looked for there,
  // so an older photo's identical buttons are never pressed.
  const keyboard = () => page.locator(SEL.messages).filter({ has: page.locator(SEL.inlineButton) }).last();

  const chat = {
    // A chat never opened before shows START instead of the composer; pressing it is /start.
    async startIfNew() {
      if (!(await page.locator(SEL.startButton).isVisible().catch(() => false))) return false;
      const before = await since();
      await page.locator(SEL.startButton).click();
      await answer(before);
      await page.locator(SEL.input).waitFor({ timeout: waits.menuMs });
      return true;
    },
    async send(text, wait = {}) {
      await page.locator(SEL.input).click();
      await page.keyboard.type(text);
      const before = await since();
      await page.keyboard.press('Enter');
      await answer(before, wait);
    },
    // A submenu button edits the keyboard in place (wait for `reveals` on that same message); an
    // action button is answered with new messages.
    async tap(label, reveals = null, wait = {}) {
      const before = await since();
      const menu = keyboard();
      await menu.locator(SEL.inlineButton).filter({ hasText: label }).first().click();
      if (reveals) await menu.locator(SEL.inlineButton).filter({ hasText: reveals }).first().waitFor({ timeout: waits.menuMs });
      else await answer(before, wait);
    },
    // `kind` 'media' sends a photo or video as Telegram does, 'file' sends the file itself.
    async upload(file, { kind = 'media', caption = '', ...wait } = {}) {
      const before = await since();
      await page.locator(SEL.attach).first().click();
      const item = kind === 'file' ? SEL.attachFile : SEL.attachMedia;
      // Other menus keep hidden items with the same words: only the open menu's own row is clicked.
      const row = page.locator(item).filter({ visible: true }).first();
      const [chooser] = await Promise.all([page.waitForEvent('filechooser'), row.click()]);
      await chooser.setFiles(file);
      const sendBtn = page.locator(SEL.attachSend).first();
      await sendBtn.waitFor({ timeout: waits.menuMs }).catch(() => {});
      if (caption) {
        await page.locator(SEL.attachCaption).first().click();
        await page.keyboard.type(caption);
      }
      if (await sendBtn.isVisible().catch(() => false)) await sendBtn.click(); else await page.keyboard.press('Enter');
      await answer(before, wait);
    },
    newestText: () => page.locator(SEL.messages).last().innerText().catch(() => ''),
  };

  // Measured once the pictures have loaded and two looks a beat apart agree.
  const steadySpan = async (keep) => {
    await page.waitForFunction(pictured, [SEL.messages, keep], { timeout: waits.menuMs }).catch(() => {});
    let was = -1;
    for (let look = 0; look < 10; look += 1) {
      const now = await page.locator(SEL.messages).evaluateAll(spanOf, keep);
      if (now === was) break;
      was = now;
      await settle(300);
    }
    return was;
  };
  // Scrolled to the bottom: the column, the composer, the line under the header and the kept tops.
  const framed = async (keep) => {
    await page.mouse.wheel(0, 8000);
    await settle(waits.settleMs);
    const head = await page.locator(SEL.header).boundingBox();
    return {
      column: await page.locator(SEL.list).boundingBox(),
      foot: await page.locator(SEL.footer).boundingBox(),
      floor: head.y + head.height + 8,
      tops: await page.locator(SEL.messages).evaluateAll((els, n) => els.slice(-n).map((el) => el.getBoundingClientRect().top), keep),
    };
  };

  // The conversation alone: the column between the header and the composer, from the top of the
  // `keep`th-newest message down. When those messages outgrow the column, the window grows to
  // hold them for the shot, so a long reply is never cut and the command above it stays in.
  chat.shot = async (runner, name, keep = 2) => {
    const view = page.viewportSize();
    await page.addStyleTag({ content: STILL_CSS }).catch(() => {});
    const chrome = (await page.locator(SEL.header).boundingBox()).height
      + (await page.locator(SEL.footer).boundingBox()).height + 48;
    let tall = Math.min(waits.maxShotHeight, Math.max(view.height, Math.ceil((await steadySpan(keep)) + chrome)));
    let frame;
    for (let pass = 0; pass < 3; pass += 1) {
      if (tall !== page.viewportSize().height) await page.setViewportSize({ width: view.width, height: tall });
      frame = await framed(keep);
      const short = frame.floor - Math.min(...frame.tops);
      if (short <= 0 || tall >= waits.maxShotHeight) break;
      tall = Math.min(waits.maxShotHeight, tall + Math.ceil(short) + 8);
    }
    const whole = frame.tops.filter((top) => top >= frame.floor);
    const top = Math.max(frame.floor, whole.length ? Math.min(...whole) - 6 : frame.floor);
    await runner.shot(page, name, { clip: { x: frame.column.x, y: top, width: frame.column.width, height: frame.foot.y - top } });
    if (tall !== view.height) await page.setViewportSize(view);
  };
  return chat;
}
