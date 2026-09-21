// Shape of scriptSpawn.js — the buffer/binary/save dance every terminal command shares.
export declare function activeIn(vscode: unknown, accept: (document: unknown) => boolean): unknown;
export declare function spawn(vscode: unknown, opts: {
  document: unknown;
  missing: string;
  locate: (document: unknown) => string | null;
  missingBinary: string;
  buildArgs: (path: string) => (string[] | null) | Promise<string[] | null>;
}): Promise<unknown>;
