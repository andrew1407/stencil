// §10 window adapters: the chat, the editor's dialogs, and voice. Each opens through the
// very toolbar button the user would click; a disabled button is a NOTE string (returned),
// never a failed plan. `null` means the adapter did what was asked.
import type { DrawingApp } from '../../core/drawingApp.js';

export interface DialogAdapters {
  /** The app's confirm, then the shared clear-conversation flow. Declined = the note. */
  clearChatConversation(): Promise<string | null>;
  /** `null` closes whatever dialog is open; a name presses that window's toolbar button. */
  openDialog(name: string | null): Promise<string | null>;
  /** The assistant panel's own show / dock / close buttons; a dock alone also shows. */
  setChatPlacement(placement?: { open?: boolean | null; dock?: string | null }): Promise<string | null>;
  /** The hands-free voice-chat toggle; an unsupported browser answers with the note. */
  setVoiceChat(on: boolean): string | null;
}

export declare const dialogAdapters: (app: DrawingApp) => DialogAdapters;
