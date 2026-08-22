// Display-shortening for image / project names — the extension's port of
// browser/js/utils.js `shortName`. Keep the two behaviourally identical (same limit,
// same head/tail split); desktop/src/support/displayName.hpp is the third port.
//
// Names come from a URL basename (see filenameFromUrl), and CDNs hand out opaque
// 60-char slugs ("MV5BODg3MzYwMjE4N15BMl5BanBnXkFtZTcwMjU5NzAzNw@@._V1_"). A list cell
// can lean on CSS text-overflow, but a name interpolated into a status SENTENCE cannot.
//
// Middle ellipsis, because both ends carry meaning: the head is what little the user
// recognises and the tail holds the extension / suffix that says WHICH item this is.
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
