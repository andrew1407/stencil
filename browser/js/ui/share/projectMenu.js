// A project's "Copy ›" entry for the row menus — its name, links, picture, layout and whole project —
// and the share button's menu. A row that is not open loads nothing until an entry is picked; its
// picture is then read and drawn off-screen in the variant picked (renderProject.js).
import { projectShareLinks, projectNameLink } from '../../core/launch/shareLinks.js';
import { readShareLayout, readShareProjectText, readShareRenderSource } from '../../core/project/share/read.js';
import { renderProject, pngOf } from './renderProject.js';
import { currentLayoutPayload } from '../../core/project/meta/projectMeta.js';
import { copyText, copyImage, shareLink, shareImage } from './deliver.js';
import { VARIANT_META } from '../../core/image/variants.js';

export const LINK_LABELS = Object.freeze({ name: 'Project link', source: 'Image link (source URL)', server: 'Server project link' });
export const LINK_ICONS = Object.freeze({ name: 'link', source: 'globe', server: 'server' });
export const IMAGE_LABELS = Object.freeze({ current: 'Current', tint: 'No lines', original: 'Original' });
export const IMAGE_ICONS = Object.freeze({ current: 'image', tint: 'palette', original: 'layers' });

const pageUrl = () => globalThis.location?.href || 'http://localhost/';
const json = (v) => JSON.stringify(v, null, 2);
const titleOf = (facts) => facts.name || 'Stencil';
const imageCopied = (v) => `${IMAGE_LABELS[v]} image copied`;

// The open editor as the same facts a row carries.
export const liveFacts = (app) => ({
  name: app.storage?.store?.getMeta?.(app.activeProjectId)?.name || app.imageBaseName || '',
  source: app.imageSource || '',
  serverUrl: app.remoteLink?.address || '',
  remoteId: app.remoteLink?.remoteId || '',
});
const rowFacts = (meta, remote) => (remote
  ? { name: meta.name, source: meta.source, serverUrl: meta.serverUrl, remoteId: meta.id }
  : { name: meta.name, source: meta.source, serverUrl: meta.address, remoteId: meta.remoteId });

// Each kind's loader, by where the project is: open here, stored, or a server listing row.
const readers = (app, { meta = null, remote = false } = {}) => {
  const c = app.projectTransfer;
  if (!meta) {
    return { image: (v) => pngOf(app.export.renderExportCanvas(v)), layout: async () => currentLayoutPayload(app),
      project: () => readShareProjectText(c, { id: app.activeProjectId }) };
  }
  const target = remote ? { remote: meta } : { id: meta.id };
  const look = { showLines: app.showLines, showPoints: app.showPoints, pointSize: app.pointSize };
  return { image: (v) => readShareRenderSource(c, target).then(({ blob, layout }) => renderProject(blob, layout, v, look)),
    layout: () => readShareLayout(c, target), project: () => readShareProjectText(c, target) };
};

// The name as an item: a click copies it; with `shares`, a double click shares its link.
export const nameItem = (facts, shares = false) => (facts.name ? {
  icon: 'clipboard', label: 'Project name', onClick: () => copyText(facts.name, 'Project name copied'),
  ...(shares ? { onDoubleClick: () => shareLink(projectNameLink(pageUrl(), facts.name), facts.name) } : {}),
} : null);

// The links a project has, as items: a click copies and a double click shares, or — `shareFirst`,
// the share button's menu — the other way round.
export const linkItems = (facts, shareFirst = false) => projectShareLinks(pageUrl(), facts).map(({ kind, url }) => {
  const copy = () => copyText(url, `${LINK_LABELS[kind]} copied`);
  const share = () => shareLink(url, titleOf(facts));
  return { icon: LINK_ICONS[kind], label: LINK_LABELS[kind], onClick: shareFirst ? share : copy, onDoubleClick: shareFirst ? copy : share };
});

// The three picture variants as items, each with the handlers `on(variant)` gives it.
const imageItems = (on) => Object.keys(IMAGE_LABELS).map((v) => ({ icon: IMAGE_ICONS[v], label: IMAGE_LABELS[v], ...on(v) }));

// The share button's menu over the open editor: Image › its three variants, then each link it
// has, flat. A click shares, a double click copies.
export const shareMenuItems = (app) => {
  const facts = liveFacts(app);
  const { image: render } = readers(app);
  const fileName = (v) => `${app.imageBaseName || 'drawing'}${VARIANT_META[v]?.suffix || ''}.png`;
  return [
    app.image ? { icon: 'image', label: 'Image', items: imageItems((v) => ({
      onClick: () => shareImage(render(v), fileName(v), titleOf(facts)),
      onDoubleClick: () => copyImage(render(v), imageCopied(v)),
    })) } : null,
    ...linkItems(facts, true),
  ].filter(Boolean);
};

// Two siblings beside "Copy project ›": "Copy entries ›" (name, links, layout, project) and "Copy image ›",
// each read or drawn only once picked. `meta` null = the open editor; `remote` marks a server-only row.
export const copyMenuItems = (app, { meta = null, remote = false } = {}) => {
  const facts = meta ? rowFacts(meta, remote) : liveFacts(app);
  const read = readers(app, { meta, remote });
  const hasImage = !!(meta || app.image);
  const copy = [
    nameItem(facts),
    ...linkItems(facts),
    { icon: 'file-text', label: 'Layout (JSON)', onClick: () => copyText(read.layout().then(json), 'Layout copied') },
    hasImage ? { icon: 'save', label: 'Project (.stencil)', onClick: () => copyText(read.project(), 'Project copied') } : null,
  ].filter(Boolean);
  const image = hasImage ? { icon: 'image', label: 'Copy image',
    items: imageItems((v) => ({ onClick: () => copyImage(read.image(v), imageCopied(v)) })) } : null;
  return [{ icon: 'copy', label: 'Copy entries', items: copy }, image].filter(Boolean);
};
