// Shapes for lib/messages.js — the cross-context message table. The channel NAMES are
// constants there; what travels on each is documented only in prose and in the handlers
// (background/handlers/*.js), so it is written down once here.
//
// Every request/response channel answers `{ok:true,…}` or `{ok:false,error}` — never a
// rejection — and a leg waiting on the editor PAGE times out rather than hang.

/** A chrome.runtime message: the channel tag plus that channel's own fields. */
export interface StencilMessage {
  type: string;
}

/** A request/response handler's reply. `ok:false` always carries a human-readable reason. */
export type Answer<T> = ({ ok: true } & T) | { ok: false; error: string; [extra: string]: unknown };

/** How an image may be imported into an editor tab that already holds one. */
export type ImportMode = 'new' | 'replace' | 'replace-keep' | 'ask';

/** What one editor tab reports about itself (lib/editorTabs.js normalises it into a row). */
export interface EditorState {
  projectId: string;
  projectName: string;
  hasImage: boolean;
  imageName: string;
  imageSize: { w: number; h: number } | null;
  incognito: boolean;
  thumbnail: string;
  projects: Array<{ id: string; name: string; active: boolean }>;
}

// ── chrome.runtime channels ─────────────────────────────────────────────────
// Fire-and-forget unless the reply type says otherwise.
export interface RuntimeMessages {
  /** ctxTarget → SW: wake the lazy worker so the context menu exists. */
  'stencil-wake': { request: {}; reply: void };
  /** ctxTarget → SW: the right-click target it resolved. */
  'stencil-ctx': { request: { src: string; kind: string; videoUrl?: string }; reply: void };
  /** page highlight → open panel: the source URL under the cursor ('' = none). */
  'stencil-hl-hover': { request: { src: string }; reply: void };
  /** editorBridge → SW: the editor's project registry. */
  'stencil-registry': { request: { projects: unknown[] }; reply: void };
  /** panel → editorBridge: switch that tab to the project for a source. */
  'stencil-editor-switch': { request: { src: string }; reply: void };
  /** overlay → SW: open a URL in a new tab. */
  'stencil-open-tab': { request: { url: string }; reply: void };
  /** devtools panel → SW: open the options page. */
  'stencil-open-options': { request: {}; reply: void };
  /** panel → SW: inject / remove the on-page 4-quadrant drop overlay. */
  'stencil-dropzones-arm': { request: { tabId: number }; reply: void };
  'stencil-dropzones-disarm': { request: { tabId: number }; reply: void };
  /** drop overlay → SW: a row was dropped in a quadrant. */
  'stencil-page-drop': { request: { zone: string; entry: unknown }; reply: void };
  /** page API → bridge → SW. */
  'stencil-page-open': { request: { target: unknown }; reply: void };
  'stencil-page-crop': { request: { target: unknown }; reply: void };
  'stencil-page-pin': { request: { target: unknown }; reply: void };
  'stencil-page-disable': { request: {}; reply: void };
  /** page API → bridge only — never relayed to the worker. */
  'stencil-page-request-sync': { request: {}; reply: void };
  'stencil-page-set-filters': { request: { filters: unknown }; reply: void };

  // ── Editor mode: the request/response group ──
  /** panel / page API → SW: every open editor tab and the state its bridge reports. */
  'stencil-editor-list': {
    request: { thumbnails?: boolean; tabId?: number };
    reply: Answer<{ editors: Array<EditorState & { tabId: number | null; ready: boolean }> }>;
  };
  /** SW / panel → editorBridge: that tab's project + image state. */
  'stencil-editor-state': { request: { thumbnail?: boolean }; reply: Answer<{ state: EditorState }> };
  /** panel / page API → SW → editorBridge: import an image into that editor tab. */
  'stencil-editor-import': {
    request: { tabId?: number; src: string; mode?: ImportMode; resource?: string; page?: string; crop?: unknown };
    reply: Answer<{ tabId: number; mode: ImportMode; projectId: string; projectName: string }>;
  };
  /** …switch that tab to one of its own projects. */
  'stencil-editor-switch-project': {
    request: { tabId?: number; projectId: string };
    reply: Answer<{ projectId: string; projectName: string }>;
  };
  /** SW → editorBridge: open that editor tab's own crop dialog. */
  'stencil-editor-crop': { request: { src: string; album?: boolean }; reply: Answer<{}> };
  /** panel / page API → SW: focus a tab and raise its window. */
  'stencil-editor-focus-tab': { request: { tabId: number }; reply: Answer<{ tabId: number; windowId: number }> };
  /** panel / page API → SW: the other open http(s) tabs an image can be pulled from. */
  'stencil-source-tabs': { request: { currentWindowOnly?: boolean }; reply: Answer<{ tabs: unknown[] }> };
  /** panel / page API → SW: scan one tab for images. */
  'stencil-scan-tab': {
    request: { tabId: number; limit?: number };
    reply: Answer<{ tabId: number; url: string; images: unknown[] }>;
  };
}

// ── window.postMessage envelopes ────────────────────────────────────────────
/** Every page↔bridge envelope is tagged with its channel; same-window only. */
export interface PostEnvelope {
  source: string;
}

/** The two id-correlated request/response channels nest their arguments under `payload`. */
export interface CorrelatedEnvelope extends PostEnvelope {
  id: string | number;
  payload?: unknown;
}

export declare const MSG: Record<string, string>;
export declare const SRC: Record<string, string>;
