// The server shots under usecases/docs/bot/img: a project created on a collaboration server,
// listed and saved back, then opened the way another app's Open In… hands it over. They need a
// bot that can reach the server, so they run only with --with-server. The /connect line carries
// a token and the /start line its payload, so no shot keeps either.
import { encodeTelegramStartPayload } from '../../../browser/js/core/launch/deepLink.js';

export function makeServerSteps({ config, runner, chat, icon, server, enabled }) {
  const shot = (name, keep) => chat.shot(runner, name, keep);
  const name = config.get('names.project');

  return Object.freeze([
    { name: 'server', run: async (ctx) => {
      if (!enabled) return console.log('  server shots skipped: run with --with-server against a bot that can reach the server');
      const token = server.mint ? await server.mint() : process.env.STENCIL_DOCS_BOT_SERVER_TOKEN;
      if (!token) return console.warn(`  server shots skipped: no token for ${server.url}`);
      await chat.upload(icon);
      await chat.send(`/connect ${server.url} ${token}`);
      if (!/Connected to/.test(await chat.newestText())) {
        return console.warn(`  server shots skipped: the bot did not connect to ${server.url}`);
      }
      await chat.send(`/create ${name}`);
      ctx.projectId = /\(id ([^,\s]+)/.exec(await chat.newestText())?.[1] ?? null;
      // An edit, then Save: the picture the edit sends back carries the server project's row. Typed,
      // not tapped: the /connect and /create replies now sit below the photo's own keyboard.
      await chat.send('/filter sepia');
      await chat.tap('Save');
      await shot('server-save', 2);
      // Last: the list's own buttons become the newest keyboard.
      await chat.send('/projects');
      await shot('server-projects', 2);
    } },
    { name: 'open-in-arrival', run: async (ctx) => {
      if (!enabled || !ctx.projectId) return;
      await chat.send('/drop');
      await chat.send(`/start ${encodeTelegramStartPayload(server.url, ctx.projectId)}`, { expect: 2 });
      await shot('open-in-arrival', 2);
    } },
  ]);
}
