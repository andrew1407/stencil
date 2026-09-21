/** Factory for one LOCAL project row; the returned fn takes (meta, { temp?, incognito? }). */
export declare const createLocalRow: (deps: Record<string, unknown>) => (meta: object, opts?: {
  temp?: boolean; incognito?: boolean;
}) => HTMLDivElement;
