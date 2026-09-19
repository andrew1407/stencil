// Shared ancestor-chain DOM for the zoomPanViewport specs: <html> > <body> > .container >
// .canvas-section > #canvas-viewport, as availContentHeight() walks it.
export const MIN_VIEWPORT_H = 120;
export const MIN_PANEL_H = 120;

/**
 * Build the ancestor chain availContentHeight() walks:
 *   <html> > <body> > .container > .canvas-section > #canvas-viewport
 * `sectionBottom` is where the viewport's own column ends (status line + drop hint
 * below it); `containerBottom` is where the whole shell ends — which the tall
 * coordinates panel can push far past the window bottom.
 *
 * The chain is modelled in full because availContentHeight() sums the bottom
 * padding/border/margin of EVERY ancestor up to <body>, not just <body>'s: the
 * .container's own bottom padding sits under the canvas column too.
 */
export const CONTAINER_PAD = 20;
export const BODY_PAD = 16;

export const installDom = ({ innerHeight, vpTop, vpBottom, sectionBottom, containerBottom, vpBorder = 2,
                      panelTop = vpTop, footerH = sectionBottom - vpBottom, containerTransform = 'none' }) => {
  const rect = (bottom) => ({ getBoundingClientRect: () => ({ bottom }) });
  const html = { className: 'html', parentElement: null };
  const body = { className: 'body', parentElement: html, classList: { contains: () => false } };
  const container = { ...rect(containerBottom), className: 'container', parentElement: body };
  const section = { ...rect(sectionBottom), className: 'canvas-section', parentElement: container };
  // The coordinates panel: the OTHER column that can outgrow the window. It sits beside the
  // canvas (same parent chain), starting at the same top the viewport does.
  const panel = { className: 'coordinates-panel', parentElement: container, style: {},
                  getBoundingClientRect: () => ({ top: panelTop }) };
  // The rows under the viewport inside its column are summed as SIBLINGS, not measured to the
  // column's bottom edge: the column is a stretched flex box, so slack is not occupied space.
  const footer = {
    className: 'coord-status', parentElement: section, nextElementSibling: null,
    getBoundingClientRect: () => ({ height: footerH }),
  };
  const viewport = {
    getBoundingClientRect: () => ({ top: vpTop, bottom: vpBottom, height: vpBottom - vpTop }),
    closest: (sel) => (sel === '.canvas-section' ? section : sel === '.container' ? container : null),
    parentElement: section,
    nextElementSibling: footer,
    style: {},
  };
  globalThis.window = { innerHeight, innerWidth: 1728 };
  globalThis.document = {
    documentElement: html,
    body,
    getElementById: (id) => (id === 'canvas-viewport' ? viewport : id === 'coord-panel' ? panel : null),
    querySelectorAll: () => [],       // the zoom-% inputs setZoom() writes back to
    activeElement: null,
  };
  // The viewport is a border-box element with a real frame (#canvas-viewport in style.css).
  // Everything else reports only the bottom inset that matters to the walk.
  const inset = (paddingBottom) => ({ paddingBottom, borderBottomWidth: '0px', marginBottom: '0px',
                                      marginTop: '0px', display: 'block' });
  globalThis.getComputedStyle = (el) => {
    if (el === viewport) {
      return { borderTopWidth: `${vpBorder}px`, borderBottomWidth: `${vpBorder}px`, paddingTop: '0px', paddingBottom: '0px' };
    }
    if (el === container) return { ...inset(`${CONTAINER_PAD}px`), transform: containerTransform };
    if (el === body) return inset(`${BODY_PAD}px`);
    return inset('0px');
  };
  viewport.panel = panel;
  return viewport;
};

// A roomy window: viewport starts 362px down, its column ends 95px below it.
export const ROOMY = { innerHeight: 953, vpTop: 362, vpBottom: 838, sectionBottom: 933 };
// 953 - 362 - ((933 - 838) + 20 container pad + 16 body pad)
export const ROOMY_AVAIL = 460;
