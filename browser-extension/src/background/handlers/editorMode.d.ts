// Shapes for background/handlers/mode.js — what only chrome.* can answer for the
// panel and `stencil.extension`: open editor tabs, other scannable tabs, one tab's
// images, and importing into an open editor. Every handler here is request/response.
import type { MessageListener } from '../editorRelay.js';

/** Keyed by the editor-mode channel names (MSG.EDITOR_LIST, .SOURCE_TABS, …). */
export declare const editorModeHandlers: Record<string, MessageListener>;
