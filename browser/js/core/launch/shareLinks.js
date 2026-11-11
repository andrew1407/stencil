// The Stencil links a project or a picture is shared by: the running app's own page URL, its
// query and fragment dropped, carrying one launch pair (contracts/launch/launch-uri.md), so a link
// opens in the app it was made in. Pure string work.

const pageBase = (pageUrl) => {
  const u = new URL(String(pageUrl));
  u.search = '';
  u.hash = '';
  return u.toString();
};
const withPair = (pageUrl, key, value) => `${pageBase(pageUrl)}?${new URLSearchParams({ [key]: value })}`;
const isWebUrl = (s) => typeof s === 'string' && /^https?:\/\/\S+$/i.test(s.trim());

export const projectNameLink = (pageUrl, name) => withPair(pageUrl, 'project', String(name));
export const sourceLink = (pageUrl, source) => withPair(pageUrl, 'source', String(source).trim());
export const serverProjectLink = (pageUrl, serverUrl, id) =>
  withPair(pageUrl, 'project', `${String(serverUrl).replace(/\/+$/, '')}/projects/${id}`);

// Every link a project has a value for, in menu order; a `data:` or `blob:` source is no link.
export const projectShareLinks = (pageUrl, { name = '', source = '', serverUrl = '', remoteId = '' } = {}) => {
  const out = [];
  if (String(name).trim()) out.push({ kind: 'name', url: projectNameLink(pageUrl, String(name).trim()) });
  if (isWebUrl(source)) out.push({ kind: 'source', url: sourceLink(pageUrl, source) });
  if (serverUrl && remoteId) out.push({ kind: 'server', url: serverProjectLink(pageUrl, serverUrl, remoteId) });
  return out;
};
