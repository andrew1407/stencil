export declare const buildLaunchUrl: (editorUrl: string, payload: Record<string, unknown>) => string;

export interface HandoffImage { name: string; source?: string; shared?: boolean; resource?: string; [key: string]: unknown; }
export interface HandoffPayload {
  dataUrl?: string; name: string; page: { size: unknown }; source: string; resource?: string;
  incognito: boolean; open?: string;
}
export declare const buildHandoff: (image: HandoffImage, opts?: { dataUrl?: string; page?: unknown;
  resource?: string; incognito?: boolean; open?: string }) => HandoffPayload;

export declare const MAX_PAYLOAD: number;

export declare const openEditorTab: (payload: HandoffPayload) => Promise<chrome.tabs.Tab>;
export declare const launchEditorModal: (payload: HandoffPayload & { tabId: number | null }) => Promise<chrome.tabs.Tab | void>;
