// Shared assistant rendering (chat panel + context-menu chat): the same conversation
// (js/llm/chatSession.js) with the same DOM. Model output is DATA — every string lands
// via textContent, never innerHTML. Re-exports the pieces so callers keep one import.

export { CHAT_SUGGESTIONS, chatDropCueHtml, chatEmptyState, chatSuggestionsHtml, typingDots } from './chatEmpty.js';
export {
  SEND_TITLE, SEND_TITLE_PLAIN, VOICE_TITLE_LISTENING, VOICE_TITLE_PAUSED,
  chatComposerActionsHtml, createSendGesture, syncComposerControls, visibleChatMoreBtn, wireChatComposer, wireChatMoreMenu, wireChatSideToggle,
} from './chatComposer.js';
export { wireComposerVoice } from './chatComposerVoice.js';
export { shrinkWrapWidth } from './chatRowDom.js';
export { renderChatLog, stickToBottom, wireChatSuggestions } from './chatTranscript.js';
export { trackPointer, wireInputSizer } from './pointerTrack.js';
export { hideThumbPreview, wireThumbPreview } from './chatThumbPreview.js';
export { chatAskCard, chatAttachmentStrip, chatConfigureButton, chatReconnectButton } from './chatCards.js';
export { CHAT_ATTACHMENTS_EVENT, chatAttachmentChips, notifyAttachmentsChanged } from './chatAttachmentChips.js';
export { CHAT_ROW_MENU_JUMP_GAP, chatRowMenuButton, chatRowMenuItems, copyChatText, rowMenuLiftFits, rowMenuLiftPx } from './chatRowMenuModel.js';
export { CHAT_POPUP_EVENT, chatPopupOpen, chatRowMenuOpen, touchMenuGesture, wireChatRowMenu } from './chatRowMenu.js';
