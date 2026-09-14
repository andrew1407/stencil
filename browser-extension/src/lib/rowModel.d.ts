import type { ImageRow } from './imageModel.js';

export interface Badge { cls: string; text?: string; html?: string; title?: string; }

type Row = ImageRow & { poster?: boolean; meta?: boolean; shared?: boolean };

export declare function rowTitle(image: Row): string;
export declare function thumbInitialSrc(image: Row, playThumb: string): string;
export declare function dimText(image: Row): string;
export declare function rowBadges(image: Row, opts?: { opened?: boolean }): Badge[];
export declare function rowOutlineClass(
  image: Row, opts?: { showServerPins?: boolean; sharedSources?: Set<string> | null; pinned?: boolean },
): string;
