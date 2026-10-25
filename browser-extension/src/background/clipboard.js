// Writes text to the system clipboard from the service worker, which has no DOM: through one
// offscreen document (offscreen/clipboard.js), made on first use and kept for the next copy.
import { MSG } from '../lib/messages.js';

const PAGE = 'src/offscreen/clipboard.html';
let opening = null;

const ensurePage = async () => {
  const url = chrome.runtime.getURL(PAGE);
  const open = await chrome.runtime.getContexts({ contextTypes: ['OFFSCREEN_DOCUMENT'], documentUrls: [url] });
  if (open.length) return;
  // Two copies in flight share one creation: a second createDocument would throw.
  opening ||= chrome.offscreen.createDocument({ url: PAGE, reasons: ['CLIPBOARD'], justification: 'Copy a Stencil link the user asked for' })
    .finally(() => { opening = null; });
  await opening;
};

// True once the text is on the clipboard.
export const copyText = async (text) => {
  try {
    await ensurePage();
    const answer = await chrome.runtime.sendMessage({ type: MSG.OFFSCREEN_COPY, text });
    return answer?.ok === true;
  } catch {
    return false;
  }
};
