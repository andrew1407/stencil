// ── Microphone level meter ─────────────────────────────────────────────────
// Feeds the voice-input visuals (the logo shine, the toolbar mic) a 0..1 loudness:
// getUserMedia → AudioContext → AnalyserNode, RMS of the time-domain samples on every
// animation frame, smoothed with an instant attack and a short exponential decay so
// the reading tracks speech without flickering. Every platform capability is injected
// (the same rule as chatController.js) so `node --test` drives it with fakes.
export const LEVEL_DECAY_MS = 150;
export const LEVEL_FFT_SIZE = 256;
const LEVEL_FLOOR = 0.003;   // below this, a frame is silence and emits nothing

// Root-mean-square of Uint8 time-domain samples (128 = silence, 0/255 = the peaks).
export const rmsOfTimeDomain = (bytes) => {
  const n = bytes?.length || 0;
  if (!n) return 0;
  let sum = 0;
  for (let i = 0; i < n; i++) {
    const s = (bytes[i] - 128) / 128;
    sum += s * s;
  }
  return Math.sqrt(sum / n);
};

// Microphone RMS values are small — amplify, then clamp to the unit range.
export const levelFromRms = (rms) => Math.max(0, Math.min(rms * 8, 1));

const browserDeps = () => {
  const nav = typeof navigator !== 'undefined' ? navigator : null;
  const win = typeof window !== 'undefined' ? window : null;
  return {
    getUserMedia: nav?.mediaDevices?.getUserMedia
      ? (c) => nav.mediaDevices.getUserMedia(c) : null,
    AudioContext: win?.AudioContext || win?.webkitAudioContext || null,
    requestAnimationFrame: win?.requestAnimationFrame ? (fn) => win.requestAnimationFrame(fn) : null,
    cancelAnimationFrame: win?.cancelAnimationFrame ? (id) => win.cancelAnimationFrame(id) : null,
  };
};

export const createLevelMeter = ({
  getUserMedia: gum,
  AudioContext: Ctx,
  requestAnimationFrame: raf,
  cancelAnimationFrame: cancelRaf,
  now = () => Date.now(),
  decayMs = LEVEL_DECAY_MS,
} = {}) => {
  // Each platform capability: the caller's where it injected one, this browser's otherwise.
  const d = browserDeps();
  const getUserMedia = gum || d.getUserMedia;
  const AudioContext = Ctx || d.AudioContext;
  const requestAnimationFrame = raf || d.requestAnimationFrame;
  const cancelAnimationFrame = cancelRaf || d.cancelAnimationFrame;

  let running = false;
  let gen = 0;            // a stop() during the pending getUserMedia releases the late stream
  let stream = null;
  let ctx = null;
  let analyser = null;
  let bytes = null;
  let frame = null;
  let level = 0;
  let lastAt = 0;
  let emit = null;

  const release = () => {
    if (frame !== null) { cancelAnimationFrame?.(frame); frame = null; }
    for (const t of stream?.getTracks?.() || []) { try { t.stop(); } catch { /* already ended */ } }
    stream = null;
    const c = ctx;
    ctx = null;
    analyser = null;
    bytes = null;
    if (c) { try { const p = c.close(); p?.catch?.(() => {}); } catch { /* already closed */ } }
  };

  const tick = () => {
    frame = null;
    if (!running || !analyser) return;
    analyser.getByteTimeDomainData(bytes);
    const raw = levelFromRms(rmsOfTimeDomain(bytes));
    const t = now();
    const dt = lastAt ? Math.max(0, t - lastAt) : 0;
    lastAt = t;
    const prev = level;
    // Instant attack, exponential decay — frame-rate independent via the real dt.
    level = raw >= prev ? raw : raw + (prev - raw) * Math.exp(-dt / decayMs);
    if (level >= LEVEL_FLOOR || prev >= LEVEL_FLOOR) emit?.(level);
    frame = requestAnimationFrame(tick);
  };

  return {
    get running() { return running; },
    get level() { return level; },
    // Resolves once the mic is open and the loop is running; rejects when the
    // platform refuses (no capability, permission denied). Never emits before then.
    async start(onLevel) {
      if (running) return;
      if (!getUserMedia || !AudioContext || !requestAnimationFrame) throw new Error('audio capture unavailable');
      running = true;
      emit = onLevel;
      const g = ++gen;
      let s;
      try {
        s = await getUserMedia({ audio: true });
      } catch (err) {
        if (g === gen) running = false;
        throw err;
      }
      if (g !== gen || !running) {   // stopped while we waited — give the mic straight back
        for (const t of s?.getTracks?.() || []) { try { t.stop(); } catch { /* ended */ } }
        return;
      }
      stream = s;
      ctx = new AudioContext();
      if (ctx.state === 'suspended') { try { await ctx.resume(); } catch { /* best effort */ } }
      analyser = ctx.createAnalyser();
      analyser.fftSize = LEVEL_FFT_SIZE;
      ctx.createMediaStreamSource(stream).connect(analyser);
      bytes = new Uint8Array(analyser.fftSize);
      lastAt = 0;
      frame = requestAnimationFrame(tick);
    },
    // Idempotent. Ends with one final 0 so a listener's visual always settles.
    stop() {
      if (!running) return;
      running = false;
      gen++;
      release();
      const e = emit;
      emit = null;
      level = 0;
      e?.(0);
    },
  };
};
