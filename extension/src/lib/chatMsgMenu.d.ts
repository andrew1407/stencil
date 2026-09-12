export type MsgRole = 'user' | 'assistant' | 'error';
export interface MsgMenuItem { id: string; label: string; icon: string; }
export declare const msgMenuItems: (role: MsgRole) => MsgMenuItem[];
export declare const msgMenuBtnSide: (role: MsgRole) => 'left' | 'right';
export declare const createMsgMenuButton: (opts: { doc: Document; renderIcon?: (name: string) => string;
  role: MsgRole }) => HTMLButtonElement;
export declare const appendToPrompt: (inputEl: HTMLTextAreaElement | HTMLInputElement, text: string) => void;

export interface MenuBox { left: number; top: number; }
export declare const clampMenuPosition: (args: { x: number; y: number; size?: { width: number; height: number };
  viewport: { width: number; height: number }; margin?: number }) => MenuBox;

export declare const MSG_MENU_JUMP_GAP: number;
export interface PillRect { left: number; right: number; top: number; bottom: number; width: number; height: number; }
export declare const msgMenuLiftPx: (btn: PillRect | null, pills?: PillRect[], gap?: number) => number;
export declare const msgMenuLiftFits: (bubble: { top: number } | null, btn: { top: number } | null, lift: number) => boolean;
export declare const menuTransformOrigin: (args: { x: number; y: number; left: number; top: number;
  size?: { width: number; height: number } }) => string;

export interface MsgTarget { role: MsgRole; [key: string]: unknown; }
export interface MsgMenu {
  el: HTMLElement;
  openFor(target: MsgTarget, place: { x: number; y: number; viewport: { width: number; height: number } }): void;
  close(): void;
  isOpen(): boolean;
}
export declare const createMsgMenu: (opts: { doc: Document; renderIcon?: (name: string) => string;
  actions?: Record<string, (target: MsgTarget) => void> }) => MsgMenu;
