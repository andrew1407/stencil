// The server writes of a co-edit session: the version-guarded layout push with its 409
// merge-and-retry loop, and the rendered result upload. `hooks` carries the controller's side:
// the toast policy, the echo clock and the filter adoption a merge needs.
import type { DrawingApp } from '../drawingApp.js';
import type { ProjectLayout } from '../project/store/projectsStore.js';
import type { RemoteLink } from './syncController.js';

export interface PushHooks {
  /** A push outcome: `state` is 'ok' | 'failed'; the controller decides whether it toasts. */
  toast(state: string, msg: string, kind: string): void;
  /** Stamps the echo clock, so the server's event for our own write is not pulled back. */
  saved(): void;
  adoptServerFilter(layout: ProjectLayout): void;
  /** A push that never converged reloads the server's state. */
  reload(): void;
  /** False once the controller detached from the project the push started on: the reply is dropped. */
  live(): boolean;
}

/** Both links name one project (address and id); the version is not compared. */
export declare const sameLink: (a: RemoteLink | null | undefined, b: RemoteLink | null | undefined) => boolean;

export interface ResultSnapshot { link: RemoteLink | null; job: import('../draw/restingPaint.js').RestingJob | null; }

/** The link after the push, or null (not linked, failed, abandoned by a project switch, or never converged — then reloaded). */
export declare const pushLayout: (app: DrawingApp, hooks: PushHooks) => Promise<RemoteLink | null>;
/** The link and what the result paints, snapshotted now; the render runs later, off this thread when it can. */
export declare const captureResult: (app: DrawingApp) => ResultSnapshot;
/** Uploads the snapshot's result; the still-open link adopts the bumped version. */
export declare const putResult: (app: DrawingApp, snap: ResultSnapshot, hooks: Pick<PushHooks, 'toast' | 'saved'>) => Promise<void>;
