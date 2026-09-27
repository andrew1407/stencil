// When the rendered result (a full-resolution PNG, the server's `result` blob) goes up: it trails
// the layout pushes a peer edits from — after the edits go quiet, or after `minGapMs` of steady
// editing — never twice inside `minGapMs`, and at once on flush (project close, explicit save).

export interface ResultUploaderOptions {
  /** ms of quiet after the latest edit before the upload. */
  idleMs: number;
  /** ms between two uploads, and the longest a steady stream of edits waits. */
  minGapMs: number;
  now?: () => number;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (handle: unknown) => void;
}

export declare class ResultUploader {
  constructor(run: () => void, opts: ResultUploaderOptions);
  /** An edit is waiting to be uploaded. */
  readonly pending: boolean;
  /** An edit happened: (re)arm the upload by the rule above. */
  markDirty(): void;
  /** Upload now if anything is pending. */
  flush(): void;
  /** Forget the pending upload (the project was unlinked). */
  cancel(): void;
}
