/** The popup/panel row shape these predicates work over. */
export interface ImageRow {
  kind: string;
  src?: string;
  videoUrl?: string;
  posterUrl?: string;
  name?: string;
  w?: number;
  h?: number;
  source?: string;
}

export declare function sourceOf(image: ImageRow): string;
export declare function posterImage(video: ImageRow): ImageRow & { kind: 'img'; poster: true };
export declare function editableSrc(image: ImageRow): string;
export declare function pinnable(image: ImageRow): boolean;
export declare function sharedMatchesSearch(image: ImageRow, search: string, regex?: boolean): boolean;
export declare function hostLabel(origin: string): string;
