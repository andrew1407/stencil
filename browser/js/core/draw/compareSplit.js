// A split compare view's halves: the untouched original over the original side (the picture
// layer, and a clean split export) and the divider bar + knob (the lines layer only). The lines
// show on the edit side alone, so a layered frame erases them from the original side.
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const { COMPARE_DIVIDER } = constants;

export const splitFraction = (app) => Math.min(1, Math.max(0, app.compareSplit ?? 0.5));

const originalRect = (ctx, app, mode) => {
  const w = app.canvas.width;
  const h = app.canvas.height;
  const f = splitFraction(app);
  ctx.beginPath();
  if (mode === 'vertical') ctx.rect(0, 0, w * f, h);
  else ctx.rect(0, 0, w, h * f);
};

export const paintOriginalSide = (ctx, app, mode) => {
  ctx.save();
  originalRect(ctx, app, mode);
  ctx.clip();
  ctx.filter = 'none';
  ctx.drawImage(app.image, 0, 0);
  ctx.restore();
};

// Erased after the lines are down, not clipped before: a clip rasterises strokes differently,
// and the one-canvas frame drew them whole before the original covered them.
export const eraseOriginalSide = (ctx, app, mode) => {
  ctx.save();
  ctx.globalCompositeOperation = 'destination-out';
  ctx.fillStyle = '#000';
  originalRect(ctx, app, mode);
  ctx.fill();
  ctx.restore();
};

// Image space, but a constant on-screen size: every measure is divided by the zoom.
export const paintDivider = (ctx, app, mode) => {
  const w = app.canvas.width;
  const h = app.canvas.height;
  const f = splitFraction(app);
  const scale = app.scale || 1;
  const knob = COMPARE_DIVIDER.knobRadiusPx / scale;
  ctx.save();
  ctx.strokeStyle = 'rgba(255,255,255,0.95)';
  ctx.fillStyle = 'rgba(255,255,255,0.95)';
  ctx.lineWidth = COMPARE_DIVIDER.lineWidthPx / scale;
  ctx.shadowColor = 'rgba(0,0,0,0.55)';
  ctx.shadowBlur = COMPARE_DIVIDER.shadowBlurPx / scale;
  ctx.beginPath();
  if (mode === 'vertical') {
    const x = w * f;
    ctx.moveTo(x, 0); ctx.lineTo(x, h);
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(x, h / 2, knob, 0, Math.PI * 2);
    ctx.fill();
  } else {
    const y = h * f;
    ctx.moveTo(0, y); ctx.lineTo(w, y);
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(w / 2, y, knob, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
};
