export declare const INLINE_WARN_CHARS: number;
export declare const INLINE_MAX_CHARS: number;
export declare const TELEGRAM_START_LIMIT: number;

export interface StencilSchemeOptions {
  scheme?: string;
  server?: string;
  id?: string;
  version?: string | number;
  src?: string;
  layout?: string | Record<string, unknown>;
  frame?: number;
  incognito?: boolean;
}

export declare function buildStencilSchemeUrl(opts?: StencilSchemeOptions): string;
export declare function encodeTelegramStartPayload(serverUrl: string, projectId: string): string | null;
export declare function buildTelegramLink(botUsername: string, payload: string): string;
