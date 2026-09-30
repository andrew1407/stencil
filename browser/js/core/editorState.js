// The editor's plain-data defaults: a fresh object per call, nothing shared between editors.
import { defaultUnitFromLocale } from '../utils.js';
import constants from '../../../common/config/constants.json' with { type: 'json' };

const { DEFAULT_VISUALS: V, DEFAULT_PAGE, HOLD_DRAW } = constants;

export const createEditorState = () => ({
  image: null,
// `originalImage` is the untouched full-res bitmap; `image` holds only the cropped region;
// `cropRect` records it in original-image pixels. Line and point coords are crop-local.
  originalImage: null,
  cropRect: null,
// Quarter turns (0..3, clockwise) applied to `originalImage` before cropping; `cropRect`
// lives in the rotated pixel space and line points ride along each turn.
  rotationQuarters: 0,
// `imageSource` = the image/video's own URL, `imageResource` = the web page it came from.
// Persisted in the layout and mirrored into project meta.
  imageSource: null,
  imageResource: null,
// "#rrggbb" for a blank project (recolourable solid background), "" for an ordinary image.
  blankColor: '',
// Opened from a portable .stencil file (bronze projects-list outline).
  fromFile: false,
  lines: [],
  currentLine: null,
  isDrawing: false,
  scale: 1,

// The pointer gesture in flight (core/pointer/gesture.js); the is* drag flags read it. The pan
// cursor delta lives in PointerController.
  gesture: 'none',

  draggingPoint: null,
  dragJustEnded: false,

  draggingSegment: null, // { lineIdx, ptIdx1, ptIdx2, startX, startY, origPt1, origPt2 }

  draggingLine: null, // { lineIdx, startX, startY, origPoints }

  zoomRectStart: null, // { imgX, imgY, cssX, cssY }
  zoomRectEnd: null, // { imgX, imgY, cssX, cssY }

  coordLineIdx: -1,
  hoveredPtIdx: -1,
  focusedPtIdx: -1,

  color: V.color,
// '' = follow this.color, matching core's Line::pointColor / pointColorOr fallback.
  pointColor: '',
  thickness: V.thickness,
  pointSize: V.pointSize,
  style: V.style,
  showPoints: true,
  showLines: true,
  imageFilter: 'none', // 'none' | 'bw' | 'sepia' | 'custom'
  filterColor: '#7c3aed',
// Compare view: transient, never persisted or synced.
  compareMode: 'none', // 'none' | 'original' | 'vertical' | 'horizontal'
  compareSplit: 0.5,   // divider position (0..1) for the split compare modes
  compareHoldOriginal: false, // Alt+Shift+O momentary "show original" override
  pageSize: DEFAULT_PAGE.size,
  customPageWidth: DEFAULT_PAGE.customWidth,
  customPageHeight: DEFAULT_PAGE.customHeight,
  selectedLineIdx: -1,
// Ctrl/⌘+Shift+click set; empty in single-select mode (selectedIndices() then falls back
// to [selectedLineIdx]). With 2+ held, selectedLineIdx is -1 and move/rotate act on all.
  selectedLines: [],
  tooltipEnabled: true,
  tooltipShowPage: true,
  tooltipShowScreen: true,
  tooltipShowCoords: true,

  allowFormulas: false,
  formulaX: '',
  formulaY: '',

// Lengths are always stored in cm; 'cm' | 'in' affects display/entry only. Seeded from
// locale; a restored layout's saved unit overrides it.
  unit: defaultUnitFromLocale(),

// holdPreview is the ghost-line cursor target (image space) while a hold stroke is active.
  holdDrawDelay: HOLD_DRAW.delayMs,
  holdPreview: null,

  drawMode: 'line',
  rectDrawStart: null, // { imgX, imgY, cssX, cssY }
  rectDrawEnd: null,
// Start with a line selected → new points/rects extend it and inherit its style; -1 = fresh line.
  continueLineIdx: -1,
  continueInsertIdx: -1,

  hoverPt: null,
  hoverLineIdx: -1,       // line under the canvas cursor → tints its Lines-list row
  listHoverLineIdx: -1,   // hovered Lines-list row → hover glow on the canvas
  mouseOverCanvas: false,
  lastMouseClientX: 0,
  lastMouseClientY: 0,

  selGlowColor: V.selGlowColor,
  hoverRingColor: V.hoverRingColor,
  focusRingColor: V.focusRingColor,
// White, not blue: a saturated fill reads as a choice already made. Shared with the
// desktop via config/constants.json.
  defaultFillColor: V.defaultFillColor,

// Mirrors storage.activeId; null = temporary editor.
  activeProjectId: null,

// { address, remoteId, version } for a server-stored project, null for a purely-local one;
// consumed by saveToServer().
  remoteLink: null,

// Armed by newEditor({ address }): the NEXT image load creates the project on that server
// (the server forbids image-less projects), then links the session.
  pendingRemoteAddress: null,

});
