// A clip as lossless PNG frames off the page's compositor (CDP screencast). Frames arrive only
// when something repaints, each stamped with when it was painted, so the GIF built from them
// plays at life speed with no video codec in between.
import fs from 'node:fs';
import path from 'node:path';

export async function startCast(page, dir) {
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const cdp = await page.context().newCDPSession(page);
  const frames = [];
  let writes = Promise.resolve();
  cdp.on('Page.screencastFrame', ({ data, metadata, sessionId }) => {
    const file = path.join(dir, `frame-${String(frames.length + 1).padStart(5, '0')}.png`);
    frames.push({ file, at: metadata.timestamp ?? Date.now() / 1000 });
    writes = writes.then(() => fs.promises.writeFile(file, Buffer.from(data, 'base64')));
    cdp.send('Page.screencastFrameAck', { sessionId }).catch(() => {});
  });
  await cdp.send('Page.startScreencast', { format: 'png', everyNthFrame: 1 });
  return {
    // Resolves { frames: [{ file, at }], end } once every frame is on disk.
    async stop() {
      const end = Date.now() / 1000;
      await cdp.send('Page.stopScreencast').catch(() => {});
      await writes;
      await cdp.detach().catch(() => {});
      return { frames, end };
    },
  };
}
