// The console logo as rows of text: the favicon's rounded panel framing the signature polyline.
// Port of core/format/logoBanner.cpp; the CLI stage paints these rows.
export type LogoMark = 'S' | 'P' | 'M';

export interface BannerLayout {
  /** Leading spaces before the left border. */
  indent: number;
  /** The lighter inner frame holding the mark. */
  frameWidth: number;
  frameHeight: number;
  /** The dark panel margin either side of the frame. */
  margin: number;
  /** frameWidth + 2 * margin. */
  panelWidth: number;
  /** The ╱ ╲ rows above and below the body (0 when compact). */
  curveRows: number;
  /** The body row the wordmark sits beside. */
  labelRow: number;
}

export declare const MARKS: readonly LogoMark[];
export declare const logoBannerLayout: (compact: boolean) => BannerLayout;
/** The mark alone (boxed false) or the rounded panel around it; no rows for an unknown mark. */
export declare const logoBanner: (mark: string, compact: boolean, boxed: boolean) => string[];
export declare const rasteriseMark: (mark: string, compact: boolean) => string[];
export declare const logoBannerRows: (mark: string, compact: boolean) => string[];
