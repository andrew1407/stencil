// Captures usecases/docs/bot/img/* from the real Telegram Web client talking to the bot. Semi-
// manual by design: Telegram needs a person to log in once, in a dedicated profile kept
// outside the repo, and the bot must be running (the live one, or `dotnet run` locally with
// this account on its allowlist). This script never starts the bot and never uses its token.
//   node usecases/capture-runner/bot.mjs [--bot <username>]
//     [--profile ~/.stencil-docs-telegram] [--with-model] [--with-server [--server <url>]]
import fs from 'node:fs';
import { chromium } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { startCollab } from './lib/collab.mjs';
import { expandHome, outDir, repoPath } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shot/runner.mjs';
import { openBotChat } from './bot/chat.mjs';
import { makeSamples } from './bot/samples.mjs';
import { makeBotSteps } from './bot/steps.mjs';
import { makeServerSteps } from './bot/serverSteps.mjs';

const config = loadCaptureConfig('bot');
const runner = makeShotRunner({ config, out: outDir('bot'), quantize: true });
const waits = config.get('waits');
const icon = repoPath(config.get('urls.localBotIcon'));
const arg = (flag, fallback) => {
  const at = process.argv.indexOf(flag);
  return at >= 0 ? process.argv[at + 1] : fallback;
};
// The STENCIL_BOT_NAME line of the bot's own env file; no other line of it is used, and
// `commented` says the line is there but still starts with '#'.
const botEnvName = () => {
  try {
    const lines = fs.readFileSync(repoPath('bot', '.env'), 'utf8').split('\n');
    const line = lines.find((l) => /^\s*(export\s+)?STENCIL_BOT_NAME\s*=/.test(l));
    const commented = !line && lines.some((l) => /^\s*#\s*(export\s+)?STENCIL_BOT_NAME\s*=/.test(l));
    return { name: line ? line.slice(line.indexOf('=') + 1).trim().replace(/^(["'])(.*)\1$/, '$2') : '', commented };
  } catch {
    return { name: '', commented: false };
  }
};
const fromBotEnv = botEnvName();
// The bot's @username is the operator's, never the repo's: --bot, else STENCIL_DOCS_BOT, else
// STENCIL_BOT_NAME from the environment or the bot's env file.
const bot = String(arg('--bot', process.env.STENCIL_DOCS_BOT || process.env.STENCIL_BOT_NAME || fromBotEnv.name))
  .trim().replace(/^@/, '');
if (!bot) {
  throw new Error(fromBotEnv.commented
    ? 'STENCIL_BOT_NAME in bot/.env is commented out: remove the leading # from that line'
    : 'name the bot to photograph: --bot <username>, STENCIL_DOCS_BOT, or STENCIL_BOT_NAME in bot/.env');
}
const profile = expandHome(arg('--profile', config.get('profileDir')));
// Whether the bot has a model and can reach a server is the operator's to know, so the shots
// that need either wait to be asked for; without them the images already there stay.
const withModel = process.argv.includes('--with-model') || process.env.STENCIL_DOCS_BOT_MODEL === '1';
const withServer = process.argv.includes('--with-server');
const serverUrl = arg('--server', null);
const server = !withServer ? null
  : serverUrl ? { url: serverUrl.replace(/\/$/, ''), mint: null, stop: async () => {} }
    : await startCollab(config);

// Telegram's own dark theme follows the emulated colour scheme.
const context = await chromium.launchPersistentContext(profile, {
  headless: false, viewport: config.get('viewport'), colorScheme: runner.themeOf('chat'),
});
const page = context.pages()[0] || await context.newPage();
const chat = await openBotChat(page, { bot, waits, profile });
const started = await chat.startIfNew();
const samples = makeSamples(icon);

console.log('bot chat');
const ctx = await runner.play(makeBotSteps({ config, runner, chat, icon, samples, withModel, waits }), { started });
await runner.play(makeServerSteps({ config, runner, chat, icon, server, enabled: withServer }), ctx);
await context.close();
await server?.stop();
await runner.finish();
