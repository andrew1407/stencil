import type { StencilNotifications } from './notifications.js';
import type { NotifyChannel } from '../../core/settings/notifyChannel.js';

export type NotifyType = 'ok' | 'fail' | 'info';
export interface NotifyOptions { onClick?: (() => void) | null; key?: string | null; shine?: boolean }

/** One way of reaching the user; `show` is true when the notice was delivered. */
export interface NotificationSink {
  /** Whether this sink can deliver at all right now. */
  isAvailable(): boolean;
  show(msg: string, type?: NotifyType, opts?: NotifyOptions): boolean;
}

/** The in-app stack: delegates to the element's own toast(). */
export declare class ToastSink implements NotificationSink {
  constructor(stack: StencilNotifications);
  isAvailable(): boolean;
  show(msg: string, type?: NotifyType, opts?: NotifyOptions): boolean;
}

/** The browser's Notification API; `api` is injected so a test can stand in for it. */
export declare class SystemSink implements NotificationSink {
  constructor(opts?: { api?: () => (typeof Notification | undefined); focus?: () => void });
  /** True only with granted permission. */
  isAvailable(): boolean;
  /** 'granted' | 'denied' | 'default' | 'unsupported'. */
  static requestPermission(api?: () => (typeof Notification | undefined)): Promise<string>;
  show(msg: string, type?: NotifyType, opts?: NotifyOptions): boolean;
}

/** The sink keyed by the channel's name, or the toasts when that one cannot deliver. */
export declare const pickSink: (channel: NotifyChannel, sinks: Record<NotifyChannel, NotificationSink>) => NotificationSink;
