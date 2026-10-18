// The secret skins (config/console/skins.json) and the frame clock they move on: a port of
// cli/src/app/skin.zig and skin/hues.zig. The CLI stage reads the words, traits and colours here.
import SKINS from '../../../../../common/config/console/skins.json' with { type: 'json' };

export const SKIN_NAMES = Object.freeze(Object.keys(SKINS.skins));
export const LIST_WORD = SKINS.listWord;
export const GOLD = SKINS.gold;
export const FRAME_MS = SKINS.frameMs;
const HUES = SKINS.hues;

export const traitsOf = (name) => SKINS.skins[name] ?? null;

// The skin a typed word puts on, any casing; null when it names none.
export const skinOf = (word) => {
  const w = String(word ?? '').trim().toLowerCase();
  return SKIN_NAMES.find((n) => SKINS.skins[n].word === w) ?? null;
};
export const isSecret = (word) => String(word ?? '').trim().toLowerCase() === LIST_WORD || skinOf(word) !== null;

// The frame clock: frame 0 is when the skin went on; `syncClock(now)` says whether it moved.
export const createSkinClock = () => {
  let start = 0, phase = 0, seed = 0, bulb = 0, bulbDue = 0;
  return {
    start(nowMs, newSeed = nowMs) { start = nowMs; phase = 0; seed = newSeed >>> 0; bulbDue = 0; },
    syncClock(nowMs) {
      const want = Math.max(0, Math.floor((nowMs - start) / FRAME_MS));
      if (want === phase) return false;
      phase = want;
      return true;
    },
    tick() { phase++; },
    frame: () => phase,
    seed: () => seed,
    // The fairy lights' next colour when its turn has come: each bulb stays lit bulbFrames.
    nextBulb() {
      if (phase < bulbDue) return null;
      bulbDue = phase + HUES.bulbFrames;
      bulb = (bulb + 1) % HUES.bulbs.length;
      return HUES.bulbs[bulb];
    },
  };
};

const hsv = (h, s, v) => {
  const c = v * s, hp = h / 60, x = c * (1 - Math.abs((hp % 2) - 1)), m = v - c;
  const [r, g, b] = hp < 1 ? [c, x, 0] : hp < 2 ? [x, c, 0] : hp < 3 ? [0, c, x] : hp < 4 ? [0, x, c] : hp < 5 ? [x, 0, c] : [c, 0, x];
  return `rgb(${Math.round((r + m) * 255)}, ${Math.round((g + m) * 255)}, ${Math.round((b + m) * 255)})`;
};

// The hue a run-painting skin gives column `col`; the bifrost slides it one column a frame.
export const hueAt = (name, col, frame) => {
  const slid = col - (traitsOf(name)?.slides ? frame : 0);
  return hsv(((slid * HUES.hueStep) % 360 + 360) % 360, 0.75, 1);
};
