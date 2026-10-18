// The accent recolour wipe over the CLI stage's header and scrollback.
export interface WipeEvent { at: number; kind: 'wash' | 'letter'; x?: number; i?: number; }
export interface WipePaint { wash: (x: number) => void; letter: (i: number) => void; settle: () => void; }
export declare const wipeFrame: (headerRows: number, cols: number, x: number, reach: number) => [number, number, number][];
export declare const ruleReach: (cols: number, x: number, reach: number) => number;
export declare const wipeSchedule: (reach: number, letters: number) => WipeEvent[];
export declare const runWipe: (opts: { reach: number; letters: number; paint: WipePaint;
  schedule?: (fn: () => void, ms: number) => unknown; cancelTimer?: (t: unknown) => void; instant?: boolean }) => () => void;
