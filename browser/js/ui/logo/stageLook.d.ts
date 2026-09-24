// What a running logo stage wears, re-resolved whenever the skin, motion, accent or theme moves.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { StageStyle } from './stageRules.js';
import type { StageCloud } from './stageCloud.js';

export interface StageLook {
  /** The cloud's style, null for a light-only show. */
  readonly style: StageStyle | null;
  /** Whether the cloud spawns: a show's own motion, else the effective motion mode. */
  readonly dusty: boolean;
  /** Whether the light is painted: always for a light-only show, else only while a cloud flies. */
  readonly glows: boolean;
  /** Replaced whenever the style or `dusty` changes, so no grain outlives its look. */
  readonly cloud: StageCloud;
  /** The mark now on stage; a redrawn one takes over once it has decoded. */
  readonly img: HTMLImageElement;
  /** Resolve everything now, exactly as a stage opened in the current state would. */
  restyle(): StageLook;
  /** Restyle only if a watched change has landed since the last resolve; once per frame. */
  refresh(): void;
  unwatch(): void;
}
export declare const createStageLook: (name: string, app: DrawingApp | null | undefined,
  doc: Document) => StageLook;
