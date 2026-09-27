// The whole right-click menu wired for real (StencilContextMenu.wire) over the permissive
// chat DOM, with a provider saved so the Assistant entry exists and its flyout chat is live.
// Its canvas fires the contextmenu that opens it; the Script entry carries an empty submenu.
import { installChatDom, scriptedController, recordNotices } from './chatSurfacesRig.js';

export const wireContextMenu = async ({ provider = 'ollama' } = {}) => {
  const doc = installChatDom();
  localStorage.setItem('drawingApp_llmSettings', JSON.stringify({ provider }));
  const notices = recordNotices(doc);
  const session = await import('../../js/llm/chat/session.js');
  session.resetChatLog();
  const app = { image: {}, lines: [], voice: null, settings: { wireFormulaInputs() {} } };
  const ctrl = scriptedController();
  session.sharedChatController(app, { create: () => ctrl });
  const $ = (id) => doc.getElementById(id);
  const menu = $('ctx-menu');
  const scriptSub = doc.createElement('div');
  scriptSub.className = 'ctx-sub';
  menu.append($('ctx-script'));
  $('ctx-script').append(scriptSub);
  const { StencilContextMenu } = await import('../../js/ui/contextMenu/contextMenu.js');
  StencilContextMenu.prototype.wire.call(menu, app);
  const openAt = (x = 200, y = 150) => $('canvas').fire('contextmenu', { clientX: x, clientY: y });
  const isOpen = () => menu.classList.contains('ctx-open');
  return {
    doc, app, ctrl, notices, session, menu, openAt, isOpen, scriptItem: $('ctx-script'),
    flyout: { item: $('ctx-assist-menu'), el: $('ctx-assist-sub'), transcript: $('ctx-assist-transcript'),
      input: $('ctx-assist-input'), send: $('ctx-assist-send') },
  };
};

export { typeAndSend } from './chatSurfacesRig.js';
