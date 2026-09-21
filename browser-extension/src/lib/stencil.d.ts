import type { Settings } from './prefs/settings.js';

export declare const DEFAULT_EDITOR_URL: string;
export declare const DEFAULT_PAGE: string;
export declare function editorOriginPattern(): Promise<string | null>;
export declare function getSettings(): Promise<Settings>;
export declare function originPattern(url: string): string | null;
export declare function setSettings(patch: Partial<Settings>): Promise<void>;

export declare function blobToDataUrl(blob: Blob): Promise<string>;
export declare function fetchAsDataUrl(url: string, opts?: { pageUrl?: string }): Promise<string>;
export declare function filenameFromUrl(url: string, fallback?: string): string;
export declare function guessMime(url: string): string;
export declare function isImageDataUrl(s: unknown): boolean;

export declare const MAX_PAYLOAD: number;
export interface EditorHandoffPayload {
  dataUrl?: string;
  name?: string;
  page?: { size?: string };
  source?: string;
  resource?: string;
  incognito?: boolean;
  open?: 'resume' | 'copy';
  crop?: { x: number; y: number; width: number; height: number };
}
export declare function buildHandoff(
  image: { name?: string; source?: string; shared?: boolean; resource?: string },
  opts?: { dataUrl?: string; page?: string; resource?: string; incognito?: boolean; open?: 'resume' | 'copy' },
): EditorHandoffPayload;
export declare function buildLaunchUrl(editorUrl: string, payload: EditorHandoffPayload): string;
export declare function launchEditorModal(payload: EditorHandoffPayload & { tabId?: number | null }): Promise<unknown>;
export declare function openEditorTab(payload: EditorHandoffPayload): Promise<unknown>;

export declare function focusTab(tab: { id: number; windowId?: number }): Promise<void>;
/** False = no open editor tab took it; the caller opens a fresh tab. */
export declare function resumeInOpenEditor(opts: { source: string; name?: string }): Promise<boolean>;

export declare const CROP_SRC_KEY: string;
export declare const CROP_META_KEY: string;
export declare function launchCrop(
  opts: { src: string; source?: string; resource?: string; tabId?: number | null },
): Promise<unknown>;
