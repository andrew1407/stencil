import type { IconDragHandle, IconDragHooks } from './iconDrag.js';
import type { DockZones } from '../chat/dockZones.js';
import type { PanelApi } from '../chat/panel/api.js';

export type ChatSpot = 'left' | 'right' | 'top' | 'bottom' | { x: number; y: number };

/** A release point's spot: the dock zone it falls in, else the point itself. */
export declare function chatSpotAt(x: number, y: number, vw: number, vh: number): ChatSpot;

export interface ChatDragOptions {
  /** The panel's surface; a drag is refused until it exists. */
  chat(): Pick<PanelApi, 'openAt'> | null | undefined;
  zones?: DockZones;
  view?(): { w: number; h: number };
  /** True while the page is phone-width (the panel is a centred modal there). */
  phone?(): boolean;
}

/** The drag hooks: zones while live, then docked or floating where the release lands. */
export declare function chatDragHooks(opts: ChatDragOptions): Required<IconDragHooks>;

export declare function wireChatDrag(btn: HTMLElement | null, chat: ChatDragOptions['chat']): IconDragHandle | null;
