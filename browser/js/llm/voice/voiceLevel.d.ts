// Microphone level meter: getUserMedia → AudioContext → AnalyserNode, RMS of the
// time-domain samples every animation frame, smoothed with an instant attack and a short
// exponential decay. Every platform capability is injected so `node --test` drives it.

export interface LevelMeterDeps {
  getUserMedia?: (constraints: MediaStreamConstraints) => Promise<MediaStream>;
  AudioContext?: typeof AudioContext;
  requestAnimationFrame?: (fn: FrameRequestCallback) => number;
  cancelAnimationFrame?: (id: number) => void;
  now?: () => number;
  decayMs?: number;
}

export interface LevelMeter {
  readonly running: boolean;
  /** 0..1 loudness, smoothed. */
  readonly level: number;
  /** Resolves once the mic is open and the loop runs; rejects when the platform refuses. */
  start(onLevel: (level: number) => void): Promise<void>;
  /** Idempotent; ends with one final 0 so a listener's visual settles. */
  stop(): void;
}

/** Decay time constant of the level reading, in ms. */
export declare const LEVEL_DECAY_MS: number;
export declare const LEVEL_FFT_SIZE: number;
/** RMS of Uint8 time-domain samples (128 = silence, 0/255 = the peaks). */
export declare const rmsOfTimeDomain: (bytes: ArrayLike<number> | null | undefined) => number;
/** Amplified and clamped to the unit range. */
export declare const levelFromRms: (rms: number) => number;
export declare const createLevelMeter: (deps?: LevelMeterDeps) => LevelMeter;
