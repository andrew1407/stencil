// The image list's lazy size probe: a row whose image has no known size is watched until it nears the
// viewport, then measured once. An observer holds its targets, so sweep() lets go of the rows a render
// dropped, and an image already being measured is not asked for again by its rebuilt row.
export const createMeasureWatch = ({ makeObserver, measure }) => {
  const watched = new Map();   // li → image, while its size is unknown
  const probing = new WeakSet();
  const observer = makeObserver((entries) => {
    for (const e of entries) {
      if (!e.isIntersecting) continue;
      const image = watched.get(e.target);
      forget(e.target);
      if (!image || probing.has(image)) continue;
      probing.add(image);
      measure(image, e.target, () => probing.delete(image));
    }
  });
  const forget = (li) => {
    observer.unobserve(li);
    watched.delete(li);
  };
  return {
    watch(li, image) {
      if (image.measured || probing.has(image)) return;
      watched.set(li, image);
      observer.observe(li);
    },
    forget,
    sweep() {
      for (const li of watched.keys()) if (!li.isConnected) forget(li);
    },
    get size() { return watched.size; },
  };
};
