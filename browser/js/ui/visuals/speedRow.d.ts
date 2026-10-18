// One speed row of the Visuals window: a number box and a slider over one motion pref.
export interface SpeedRow { paint: (value: number | string) => void; }
export declare const wireSpeedRow: (opts: {
  range: HTMLInputElement; number: HTMLInputElement; key: 'speed' | 'consoleSpeed';
  app: { settings: { setMotion: (key: string, value: unknown) => Record<string, number> } };
}) => SpeedRow;
