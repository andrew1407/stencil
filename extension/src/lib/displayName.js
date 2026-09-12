// Port of browser/js/utils.js `shortName` (desktop/src/support/displayName.hpp is the third):
// a middle ellipsis for names interpolated into a SENTENCE, where text-overflow can't reach.
export const NAME_DISPLAY_CHARS = 28;

export const shortName = (name, limit = NAME_DISPLAY_CHARS) => {
  const s = String(name ?? '');
  if (s.length <= limit) return s;
  // One char reserved for the ellipsis; the head gets the extra char on odd splits.
  const keep = limit - 1;
  const head = Math.ceil(keep / 2);
  const tail = keep - head;
  return `${s.slice(0, head)}…${tail > 0 ? s.slice(-tail) : ''}`;
};
