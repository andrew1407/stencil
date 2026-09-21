// Shapes for llm/chatListing.js — the context listing the model reads (contract §8):
// the scanned-image set and the open-tabs set as prompt text, plus the dropped-URL →
// listing-index routing. Re-exported through controller.js.
import type { ScanEntry } from '../lib/image/scan.js';
import type { Attachment } from './chatController.js';

export type ListingKind = 'img' | 'background' | 'poster' | 'video' | 'icon';

export declare const LISTING_LIMIT: number;
export declare const LISTING_NAME_CHARS: number;
export declare const LISTING_ALT_CHARS: number;
export declare const TABS_LIMIT: number;
export declare const TAB_TITLE_CHARS: number;

export declare function listingKind(item: ScanEntry | null | undefined): ListingKind;
export declare function buildListing(
  items: ScanEntry[],
  opts?: { formatOfItem?: (item: ScanEntry) => string },
): string;
export declare function buildTabsListing(tabs: Array<{ title?: string; url?: string }>): string;
/** Index of the listing entry `url` refers to, or -1. */
export declare function matchListingIndex(items: ScanEntry[], url: string): number;
export declare function attachmentNote(attachments: Attachment[]): string;
