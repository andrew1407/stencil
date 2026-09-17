// The one home for the stencil:* channels: every announce/listen goes through here, so
// config/events.json has a single caller. Delivery IS a window event — the contract the
// extension's content scripts read. Nothing throws where there is no DOM.

/** The channel table, config/events.json: key → 'stencil:…' event name. */
export interface EventChannels {
  ready: string;
  themeChanged: string;
  accentChanged: string;
  motionChanged: string;
  fullscreenChanged: string;
  registryChanged: string;
  connectionsChanged: string;
  switchToSource: string;
  llmSettingsChanged: string;
  chatLayoutChanged: string;
  chatAttachmentsChanged: string;
  chatPopup: string;
  voiceStateChanged: string;
  voiceSettingsChanged: string;
}

export declare const EVENTS: EventChannels;

/** No `detail` sends a plain Event. False when there is nothing to dispatch on. */
export declare const publish: (channel: string, detail?: unknown, opts?: { target?: EventTarget | null }) => boolean;
/** Returns the unsubscribe. */
export declare const subscribe: (
  channel: string,
  handler: (e: Event) => void,
  opts?: { target?: EventTarget | null; once?: boolean },
) => () => void;
/** `ready` fires once on `document`, handing every <stencil-*> element the app. */
export declare const publishReady: (app: unknown) => boolean;
export declare const onReady: <A = unknown>(cb: (app: A) => void) => () => void;
