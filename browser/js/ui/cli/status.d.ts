// The status line under the CLI stage's logo and the gestures on the image name.
import type { StatusLine } from '../../console/shell/status.js';
import type { CliPeek } from './peek.js';
export declare const paintStatus: (el: HTMLElement, app: object) => StatusLine;
export declare const wireStatusGestures: (el: HTMLElement, peek: CliPeek, opts?: {
  clickMs?: number; schedule?: (fn: () => void, ms: number) => unknown; cancel?: (t: unknown) => void;
}) => { over: () => boolean; dispose: () => void };
