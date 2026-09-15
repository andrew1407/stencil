// External launch: the `#stencil=<encodeURIComponent(JSON)>` fragment and the extension
// bridge's import into THIS tab. Schema and precedence live in deepLink.js
// (normalizeLaunchPayload); each function takes the app and reuses its loaders.
import type { DrawingApp } from './drawingApp.js';
import type { CropRectInput, RemoteLayout } from './imageLoadFlow.js';

/** A page size handed in by a launch; width/height are cm, read only for 'custom'. */
export interface LaunchPage { size?: string; width?: number | string; height?: number | string; }

/** What normalizeLaunchPayload returns: one image source, plus the common fields. */
export interface NormalizedLaunch {
  kind: 'server' | 'dataUrl' | 'src';
  server?: { url: string; id: string; version: number };
  dataUrl?: string;
  src?: string;
  name: string | null;
  crop: CropRectInput | null;
  noCrop: boolean;
  page: LaunchPage | null;
  source: string | null;
  resource: string | null;
  open: string | null;
  incognito: boolean;
  layout: RemoteLayout | null;
}

export type ImportMode = 'new' | 'replace' | 'replace-keep';

/** Base name without its file extension. */
export declare const stripExt: (name: string | null | undefined) => string;
/** Fetches the launch's bytes and loads them; rejects on a failed fetch. */
export declare const importInlineImage: (app: DrawingApp, launch: NormalizedLaunch, opts?: { mode?: ImportMode }) => Promise<void>;
/** Consumes the fragment once (stripped from the URL) and routes it through the loaders. */
export declare const MAX_LAUNCH_SCRIPT: number;
export declare const applyExternalLaunch: (app: DrawingApp) => Promise<void>;
/** The extension bridge's entry point: 'new' resets to a blank editor first. */
export declare const importExternalImage: (app: DrawingApp, launch: NormalizedLaunch, opts?: { mode?: ImportMode }) => Promise<void>;
/** Switches to an existing project for this image; true when it switched. */
export declare const resumeBySource: (app: DrawingApp, source: string | null, name: string | null | undefined) => boolean;
/** Connects (asking first for an unknown origin) and opens the project, incognito or linked. */
export declare const applyServerLaunch: (app: DrawingApp, launch: NormalizedLaunch) => Promise<void>;
export declare const setExternalPage: (app: DrawingApp, page: LaunchPage) => void;
