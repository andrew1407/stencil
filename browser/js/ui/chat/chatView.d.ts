// Shared assistant rendering (chat panel + context-menu chat) — re-exports so callers
// keep one import. See each named module for the real shapes.
export { CHAT_SUGGESTIONS, chatDropCueHtml, chatEmptyState, chatSuggestionsHtml, typingDots } from './chatEmpty.js';
export {
  SEND_TITLE, SEND_TITLE_PLAIN, VOICE_TITLE_LISTENING, VOICE_TITLE_PAUSED,
  chatComposerActionsHtml, createSendGesture, syncComposerControls, visibleChatMoreBtn, wireChatComposer, wireChatMoreMenu, wireChatSideToggle,
} from './composer/chatComposer.js';
export { wireComposerVoice } from './composer/chatComposerVoice.js';
export { shrinkWrapWidth } from './row/chatRowDom.js';
export { renderChatLog, stickToBottom, wireChatSuggestions } from './chatTranscript.js';
export { trackPointer, wireInputSizer } from '../canvas/pointerTrack.js';
export { hideThumbPreview, wireThumbPreview } from './composer/chatThumbPreview.js';
export { chatAskCard, chatAttachmentStrip, chatConfigureButton, chatReconnectButton } from './row/chatCards.js';
export { CHAT_ATTACHMENTS_EVENT, chatAttachmentChips, notifyAttachmentsChanged } from './composer/chatAttachmentChips.js';
export { CHAT_ROW_MENU_JUMP_GAP, chatRowMenuButton, chatRowMenuItems, copyChatText, rowMenuLiftFits, rowMenuLiftPx } from './row/chatRowMenuModel.js';
export { CHAT_POPUP_EVENT, chatPopupOpen, chatRowMenuOpen, touchMenuGesture, wireChatRowMenu } from './row/chatRowMenu.js';
