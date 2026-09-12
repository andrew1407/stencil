// ── §1 coordinate re-mapping (executor-side) ────────────────────
// Plan coordinates are in the frame of the working image the model SAW; crop/rotate change
// it mid-plan, so the executor composes a running affine map (quarter turns + integer
// translations, exact arithmetic) and pushes every later layout point through it.

export const identityFrame = () => ({ a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 });

// Compose `n` AFTER `f` (points flow f → n), in place.
export const composeFrame = (f, n) => {
  const { a, b, c, d, tx, ty } = f;
  f.a = n.a * a + n.b * c;
  f.b = n.a * b + n.b * d;
  f.tx = n.a * tx + n.b * ty + n.tx;
  f.c = n.c * a + n.d * c;
  f.d = n.c * b + n.d * d;
  f.ty = n.c * tx + n.d * ty + n.ty;
};

export const mapFramePoint = (f, p) => ({ x: f.a * p.x + f.b * p.y + f.tx, y: f.c * p.x + f.d * p.y + f.ty });
