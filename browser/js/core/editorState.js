// ── The editor's plain-data defaults ────────────────────────────
// Extracted from DrawingApp's constructor: every field that is a value, not a collaborator
// or a DOM node. A fresh object per call — nothing here is shared between editors.
import { defaultUnitFromLocale } from '../utils.js';

export const createEditorState = () => ({
  image: null,
  // Crop support: `originalImage` = untouched full-res bitmap; working `image` =
  // canvas holding only the cropped (page-shaped) region; `cropRect` records it in
  // original-image pixels. Line and point coords are crop-local. See applyCrop / #buildCroppedImage.
  originalImage: null,
  cropRect: null,
  // Non-destructive 90° rotation: quarter-turn count (0..3, clockwise) applied to
  // `originalImage` before cropping. Original is never modified; `cropRect` lives in
  // the rotated pixel space and line points ride along each turn. See rotateImage / #rotatedOriginalCanvas.
  rotationQuarters: 0,
  // Provenance: `imageSource` = image/video's own URL, `imageResource` = web page it
  // came from. Both null for plain local uploads; set by add-by-URL + extension hand-off.
  // Persisted in the layout and mirrored into project meta.
  imageSource: null,
  imageResource: null,
  // Active session's blank-fill colour ("#rrggbb"), or "" for an ordinary image project. Set by
  // createBlankImage, restored on open, persisted into project meta (storage.js). Non-empty ⇔
  // this is a blank project (whose solid background can be recoloured after creation).
  blankColor: '',
  // True when opened from a portable .stencil file (provenance → bronze projects-list outline).
  fromFile: false,
  lines: [],
  currentLine: null,
  isDrawing: false,
  scale: 1,

  // Pan state (Alt+drag) — delta-based, with optional Shift speed-up. The pan cursor delta
  // lives in PointerController; only the isPanning flag is shared editor state.
  isPanning: false,

  // Point drag state (Alt+hover+drag on point)
  draggingPoint: null, // { lineIdx, ptIdx, point }
  isDraggingPoint: false,
  dragJustEnded: false,

  // Segment drag state (Alt+drag on a line segment between two points)
  isDraggingSegment: false,
  draggingSegment: null, // { lineIdx, ptIdx1, ptIdx2, startX, startY, origPt1, origPt2 }

  // Whole-line drag state (Alt+Shift+drag on any part of a line)
  isDraggingLine: false,
  draggingLine: null, // { lineIdx, startX, startY, origPoints }

  // Zoom rect state (Shift+left-drag)
  isZoomRectDragging: false,
  zoomRectStart: null, // { imgX, imgY, cssX, cssY }
  zoomRectEnd: null, // { imgX, imgY, cssX, cssY }

  // Coord table state
  coordLineIdx: -1,   // which line is shown in table
  hoveredPtIdx: -1,   // hovered row in table
  focusedPtIdx: -1,   // clicked/focused row in table

  color: '#FFFF00',
  // Default point colour for new lines. '' = follow this.color, matching
  // core's Line::pointColor / pointColorOr fallback.
  pointColor: '',
  thickness: 2,
  pointSize: 4,
  style: 'solid',
  showPoints: true,
  showLines: true,
  imageFilter: 'none', // 'none' | 'bw' | 'sepia' | 'custom'
  filterColor: '#7c3aed', // custom tint color
  // Compare view: the edited result against the untouched original (crop + rotation only).
  // 'original' shows the original alone; 'vertical'/'horizontal' split with a movable
  // divider. Transient view state — never persisted or synced.
  compareMode: 'none', // 'none' | 'original' | 'vertical' | 'horizontal'
  compareSplit: 0.5,   // divider position (0..1) for the split compare modes
  compareHoldOriginal: false, // Alt+Shift+O momentary "show original" override
  pageSize: 'A3',
  customPageWidth: 21,
  customPageHeight: 29.7,
  selectedLineIdx: -1,
  // Multi-line selection set (Ctrl/⌘+Shift+click). Empty in single-select mode —
  // selectedIndices() then falls back to [selectedLineIdx]. With 2+ lines held,
  // selectedLineIdx is -1 (the single-line editor hides) and move/rotate act on all.
  selectedLines: [],
  // Tooltip visibility (persisted)
  tooltipEnabled: true,
  tooltipShowPage: true,
  tooltipShowScreen: true,
  tooltipShowCoords: true,

  // Coordinate formula transforms
  allowFormulas: false,
  formulaX: '', // empty = identity
  formulaY: '',

  // Display unit for page/length readouts: 'cm' or 'in'. Lengths are always stored in
  // cm; this only affects display/entry. Default seeded from locale (US/imperial → in,
  // else cm); a restored layout's saved unit overrides it.
  unit: defaultUnitFromLocale(),

  // Hold-to-draw (see ./holdDraw.js): delay (ms) is configurable; holdPreview is the
  // ghost-line cursor target (image space) while a hold stroke is active.
  holdDrawDelay: 500,
  holdPreview: null,

  // ── Drawing mode: 'line' (click points) or 'rect' (drag rectangle) ──
  drawMode: 'line',
  isRectDrawDragging: false,
  rectDrawStart: null, // { imgX, imgY, cssX, cssY }
  rectDrawEnd: null,
  // Continuation drawing: Start with a line selected → new points/rects extend that
  // line (connecting to its last/focused point) and inherit its style. -1 = fresh line.
  continueLineIdx: -1,
  continueInsertIdx: -1,

  // ── Hover tracking (for hover ring on any point, Ctrl/Shift tooltip refresh) ──
  hoverPt: null,          // { lineIdx, ptIdx } currently hovered on canvas
  hoverLineIdx: -1,       // line under the canvas cursor → tints its Lines-list row
  listHoverLineIdx: -1,   // hovered Lines-list row → hover glow on the canvas
  mouseOverCanvas: false,
  lastMouseClientX: 0,
  lastMouseClientY: 0,

  // ── Configurable visuals (persisted) ──
  selGlowColor: '#ffc800', // selection highlight glow (lines + points)
  hoverRingColor: '#7c3aed', // hover ring around points
  focusRingColor: '#7c3aed', // focused/clicked point ring
  // White, not blue: a fill is paint you put ON the picture, and the neutral one is the
  // least surprising thing for the swatch to start at (a saturated blue reads as a choice
  // already made). Shared with the desktop via config/constants.json.
  defaultFillColor: '#ffffff',

  // ── Multi-project state ──
  // The active project id mirrors storage.activeId; null = temporary editor.
  activeProjectId: null,

  // Link to a server-stored project for the current editing session, or null for
  // a purely-local one. { address, remoteId, version }; set when a remote project
  // is opened or a local create targets a server, consumed by saveToServer().
  remoteLink: null,

  // One-shot server address armed by newEditor({ address }): the NEXT image load
  // creates the project on it (with real bytes — the server forbids image-less
  // projects), then links the session. Consumed/cleared by the next loadImageFromFile.
  pendingRemoteAddress: null,

});
