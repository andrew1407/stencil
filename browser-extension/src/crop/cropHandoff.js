// "Keep original" sends the image + the rect; "Cut cropped part" bakes the region.
export const buildHandoffPayload = (state, imgEl, { mode, incognito }) => {
  const page = state.page === 'custom'
    ? { size: 'custom', width: state.customW, height: state.customH }
    : { size: state.page };
  const c = state.crop;
  if (mode === 'apply') {
    return {
      dataUrl: state.dataUrl,
      name: state.name,
      crop: { x: c.x, y: c.y, w: c.width, h: c.height },   // canonical wire spelling
      page,
      source: state.source,
      resource: state.resource,
      incognito,
    };
  }
  const canvas = document.createElement('canvas');
  canvas.width = c.width;
  canvas.height = c.height;
  canvas.getContext('2d').drawImage(imgEl, c.x, c.y, c.width, c.height, 0, 0, c.width, c.height);
  const dot = state.name.lastIndexOf('.');
  return {
    dataUrl: canvas.toDataURL('image/png'),
    name: (dot > 0 ? state.name.slice(0, dot) : state.name) + '-crop.png',
    crop: { x: 0, y: 0, w: c.width, h: c.height },   // canonical wire spelling
    page,
    source: state.source,
    resource: state.resource,
    incognito,
  };
};
