// What a pointer may hit is what the canvas draws (renderer.js #paintAnnotations): a hidden point is
// never a point target, hidden lines never a segment target, and with both hidden nothing is.
// Desktop twin: model/markHits.hpp.
import { findLineAt, findNearestPointWithIdx, findNearestSegmentWithIdx } from '../draw/hitTest.js';
import { holdDrawTarget } from '../draw/holdDraw.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const { HIT } = constants;

// A flag never set counts as shown, as both start on (core/editorState.js); a set one is read as
// the renderer reads it.
const shown = (flag) => flag === undefined || !!flag;
export const shownMarks = (app) => ({ points: shown(app.showPoints), lines: shown(app.showLines) });

// findLineAt over what shows: the stroke alone while points are hidden, the points alone while
// lines are, each within `threshold`; -1 when neither shows.
export const lineAt = (lines, shown, x, y, threshold) => {
  if (shown.points && shown.lines) return findLineAt(lines, x, y, threshold);
  const hit = shown.lines ? findNearestSegmentWithIdx(lines, x, y, threshold)
    : shown.points ? findNearestPointWithIdx(lines, null, x, y, threshold) : null;
  return hit ? hit.lineIdx : -1;
};

// A hidden kind's radius is 0, which reaches nothing: a hold never continues a hidden point or
// inserts into a hidden line, it starts a fresh one.
export const holdTargetAt = (lines, shown, x, y, radius = HIT.grabRadiusPx) =>
  holdDrawTarget(lines, x, y, { pointThreshold: shown.points ? radius : 0, segThreshold: shown.lines ? radius : 0 });
