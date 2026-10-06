/** 'open' = save or incognito (an image, a .stencil); 'layout' = a .json; 'script' = a .stc. */
export type DropKind = 'open' | 'layout' | 'script';

/** At the drop, by the file's name (and MIME for a .json). */
export declare const dropKindOfFile: (file: { name?: string; type?: string } | null | undefined) => DropKind;
/** Mid-drag, from dataTransfer.types and each item's MIME type (names are hidden then). */
export declare const dropKindOfDrag: (types: readonly string[] | null | undefined, itemTypes?: readonly string[]) => DropKind;
