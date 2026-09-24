export type NotifyChannel = 'toast' | 'system';

export declare const NOTIFY_STORAGE_KEY: string;
/** Fired after every change (eventBus/appBus.js EVENTS.notifyChannelChanged). */
export declare const NOTIFY_EVENT: string;
export declare const NOTIFY_CHANNELS: NotifyChannel[];
export declare const DEFAULT_NOTIFY_CHANNEL: NotifyChannel;
/** [channel, label] pairs for the Visuals select; the desktop combo mirrors them. */
export declare const NOTIFY_CHANNEL_LABELS: [NotifyChannel, string][];

/** An unrecognized value falls back to DEFAULT_NOTIFY_CHANNEL. */
export declare const normalizeNotifyChannel: (v: unknown) => NotifyChannel;
/** The channel in force. */
export declare const notifyChannel: () => NotifyChannel;
/** Persists, announces, returns the channel now in force. */
export declare const setNotifyChannel: (v: unknown) => NotifyChannel;
/** Tests only: re-read the store. */
export declare const reloadNotifyChannel: () => NotifyChannel;
