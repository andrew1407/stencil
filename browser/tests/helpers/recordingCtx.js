// A 2D-context stand-in that records every call and property set, in order, so a test can
// assert on what the code PAINTED — geometry, radii, pass order — instead of on its source.
export const recordingCtx = (canvas = { width: 10, height: 10 }) => {
  const calls = [];
  const ctx = new Proxy({ canvas }, {
    get: (t, k) => (k in t ? t[k] : (...args) => { calls.push([k, ...args]); }),
    set: (t, k, v) => { t[k] = v; calls.push([`set:${String(k)}`, v]); return true; },
  });
  return { ctx, calls };
};

// The argument lists of one recorded method, in the order it was called.
export const argsOf = (calls, name) => calls.filter(([k]) => k === name).map(([, ...a]) => a);
// Where the first call to `name` sits in the recording, or -1.
export const indexOf = (calls, name) => calls.findIndex(([k]) => k === name);
