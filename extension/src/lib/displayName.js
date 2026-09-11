// Port of browser/js/utils.js `shortName` — keep the two behaviourally identical (same
// limit, same head/tail split); desktop/src/support/displayName.hpp is the third port.
// Names come from a URL basename, and CDNs hand out opaque 60-char slugs; a name
// interpolated into a status SENTENCE can't lean on CSS text-overflow. Middle ellipsis
// because both ends carry meaning — the recognisable head and the suffix.
export const NAME_DISPLAY_CHARS = 28;

export const shortName = (name, limit = NAME_DISPLAY_CHARS) => {
  const s = String(name ?? '');
  if (s.length <= limit) return s;
  // Reserve one char for the ellipsis; give the extra char to the head on odd splits.
  const keep = limit - 1;
  const head = Math.ceil(keep / 2);
  const tail = keep - head;
  return `${s.slice(0, head)}…${tail > 0 ? s.slice(-tail) : ''}`;
};
