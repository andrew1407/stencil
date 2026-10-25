// The Visuals window's small rows: a speed row over one motion pref, and voice input's
// Send-after-pause knob.
export interface SpeedRow { paint: (value: number | string) => void; }
export declare const wireSpeedRow: (opts: {
  range: HTMLInputElement; number: HTMLInputElement; key: 'speed' | 'consoleSpeed';
  app: { settings: { setMotion: (key: string, value: unknown) => Record<string, number> } };
}) => SpeedRow;
/** Wires the Visuals & Settings modal's Send-after-pause row to settings.js. */
export declare function wireVoiceSilenceRow(): { populate(): void; reset(): void };
