// The picture under the CLI stage's header: editing or original, no animation.
export type PeekKind = 'editing' | 'original';
export interface CliPeek {
  show: (kind: PeekKind, opts?: { magnified?: boolean }) => void;
  hide: () => void;
  shown: () => PeekKind | null;
  toggle: (kind: PeekKind) => void;
  magnify: (on: boolean) => void;
  refresh: () => void;
  isHeld: () => boolean;
  hold: (kind: PeekKind, opts?: { magnified?: boolean }) => void;
  release: () => void;
}
export declare const createCliPeek: (app: object, slot: HTMLElement) => CliPeek;
