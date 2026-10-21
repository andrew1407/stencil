// The chat shots under usecases/docs/bot/img, in the order they are taken: the menus, the edits,
// the pen, the files a chat can send, and — only when the bot has a model behind it — the
// assistant. Each scenario that needs a clean picture sends the icon again first.

const PROGRESS = Object.freeze({ script: /Running your script/, plan: /Working on your request/ });

export function makeBotSteps({ config, runner, chat, icon, samples, withModel, waits }) {
  const shot = (name, keep) => chat.shot(runner, name, keep);
  const freshPhoto = () => chat.upload(icon);
  const skipped = (name) => console.log(`  ${name} skipped: run with --with-model when the bot has one`);

  return Object.freeze([
    { name: 'start-menu', run: async (ctx) => {
      if (!ctx.started) await chat.send('/start');
      await shot('start-menu');
    } },
    { name: 'photo-edit-menu', run: async () => { await freshPhoto(); await shot('photo-edit-menu', 1); } },
    { name: 'crop', run: async () => { await chat.send('/crop x1=10% x2=90% y1=10% y2=90%'); await shot('crop'); } },
    { name: 'filter', run: async () => {
      await chat.tap('Filter', 'Sepia');
      await shot('filter-menu', 1);
      await chat.tap('B&W');
      await shot('filter-bw', 1);
    } },
    { name: 'draw-rect', run: async () => { await chat.send('/draw rect 20%,20% 80%,80%'); await shot('draw-rect'); } },
    // The keyboards behind the edit keyboard's buttons; each one edits the newest message in place.
    { name: 'menus', run: async () => {
      for (const [name, button, reveals] of [['menu-edit', 'Edit', 'Crop'], ['menu-draw', 'Draw', 'Undo line'],
        ['menu-download', 'Download', 'Project']]) {
        await chat.tap(button, reveals);
        await shot(name, 1);
        await chat.tap('Back', 'Filter');
      }
    } },
    { name: 'pen', run: async () => {
      for (const line of ['/color #e11d48', '/thickness 3', '/style dashed']) await chat.send(line);
      await chat.send('/draw line 12%,88% 50%,12% 88%,88%');
      await shot('pen', 8);
    } },
    { name: 'undo', run: async () => { await chat.tap('Undo'); await shot('undo', 1); } },
    { name: 'rename-describe', run: async () => {
      await chat.tap('Rename');
      await chat.send(config.get('names.project'));
      await chat.tap('Describe');
      await chat.send(config.get('names.description'));
      await shot('rename-describe', 6);
    } },
    { name: 'status', run: async () => { await chat.send('/status'); await shot('status'); } },
    { name: 'script', run: async () => {
      await freshPhoto();
      await chat.send('/script @crop 10% ; @filter sepia ; @line (0,0) (100%,100%)',
        { expect: 2, notice: PROGRESS.script });
      await shot('script', 4);
    } },
    // The files a chat can send: a script, a layout, a project and a video.
    { name: 'upload-script', run: async () => {
      await freshPhoto();
      await chat.upload(samples.script, { kind: 'file', expect: 2, notice: PROGRESS.script });
      await shot('upload-script', 3);
    } },
    { name: 'upload-layout', run: async () => {
      await freshPhoto();
      await chat.upload(samples.layout, { kind: 'file' });
      await shot('upload-layout', 2);
    } },
    { name: 'upload-project', run: async () => {
      await chat.upload(samples.project, { kind: 'file' });
      await shot('upload-project', 2);
    } },
    { name: 'video-frame', run: async () => {
      await chat.upload(samples.video, { expect: 2 });
      await chat.send('/frame 30');
      await shot('video-frame', 5);
    } },
    { name: 'blank', run: async () => { await chat.send('/blank b5 black'); await shot('blank'); } },
    { name: 'help', run: async () => { await chat.send('/help'); await shot('help'); } },
    // Chat mode needs no model to switch on; a turn in it does.
    { name: 'chat', run: async () => {
      await freshPhoto();
      await chat.tap('Chat');
      await shot('chat-mode', 1);
      if (withModel) {
        await chat.send(config.prompt('botChat'), { expect: 2, notice: PROGRESS.plan, timeout: waits.promptMs });
        await shot('chat-turn', 3);
      } else skipped('chat-turn');
      // Typed, not tapped: a turn's photo carries a newer keyboard than the chat-mode one.
      await chat.send('/chat off');
    } },
    { name: 'prompt', run: async () => {
      if (!withModel) return skipped('prompt');
      await freshPhoto();
      await chat.send(`/prompt ${config.prompt('real')}`, { expect: 2, notice: PROGRESS.plan, timeout: waits.promptMs });
      await shot('prompt', 3);
    } },
  ]);
}
