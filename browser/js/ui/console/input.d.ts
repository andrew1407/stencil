// The prompt line's keys: Enter runs, Tab completes, Up/Down recall.
export declare const wireConsoleInput: (input: HTMLInputElement, opts: {
  run: (line: string) => unknown; classify?: (line: string) => string;
  complete: (line: string) => string | null;
  history: { up: () => string; down: () => string; atEnd: () => boolean; readonly entries: string[] };
}) => void;
