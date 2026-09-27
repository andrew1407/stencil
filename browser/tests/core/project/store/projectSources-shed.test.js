// A project whose source text was shed under quota keeps only the reference, and that reference
// stays inside the registry and its dedupe: the .stencil export, a hand-off, stencil.current.source
// and the Links field all show no source instead, while a real URL or data URL still goes out as is.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../../helpers/dom.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';
import { createStencil, withProjects } from '../../../helpers/stencilApiRig.js';

const win = Object.assign(new EventTarget(), {
  matchMedia: () => ({ matches: false, addEventListener() {}, removeEventListener() {} }),
  innerWidth: 1000, innerHeight: 800,
});
installDom({ autoCreateById: true }, { window: win });

const { ProjectsStore } = await import('../../../../js/core/project/store/projectsStore.js');
const { sourceRef, shedSource, portableSource } = await import('../../../../js/core/project/store/projectSources.js');
const { projectFileState } = await import('../../../../js/core/project/fileIO.js');
const { buildProjectFile } = await import('../../../../js/core/project/file.js');
const { openInLaunchPayload } = await import('../../../../js/core/launch/payload.js');
const { StencilLinksModal } = await import('../../../../js/ui/meta/linksModal.js');
const { publish, EVENTS } = await import('../../../../js/eventBus/appBus.js');

const A = `data:image/png;base64,${'A'.repeat(20000)}`;
const REF = sourceRef(A);
const IMG = 'data:image/png;base64,SU1H';

// The shed project as the editor holds it once reopened: the reference in the registry, the
// payload and the live imageSource alike.
const shedApp = (source = REF) => {
  const store = new ProjectsStore(createMemoryStorage());
  store.upsert({ id: 'p1', name: 'shot', thumbnail: null, source: A }, shedSource({ image: IMG, layout: { imageSource: A } }));
  store.upsert({ id: 'p2', name: 'kept', thumbnail: null, source: A }, { image: IMG, layout: { imageSource: A } });
  return {
    store, activeProjectId: 'p1', imageSource: source, imageResource: null, image: {}, imageDataUrl: IMG,
    imageBaseName: 'shot', imageExt: 'png', blankColor: '', storage: { store },
    canvas: { width: 2, height: 2 }, lines: [],
  };
};

test('the registry keeps the reference, so the shed project still dedupes', () => {
  const { store } = shedApp();
  assert.equal(store.getMeta('p1').source, REF);
  assert.equal(store.get('p1').payload.layout.imageSource, REF);
  assert.deepEqual(store.findByImage(A, '').map((m) => m.id).sort(), ['p1', 'p2']);
});

test('the .stencil export carries no source for a shed project, the full URL otherwise', () => {
  const doc = buildProjectFile(projectFileState(shedApp(), { includeTheme: false }));
  assert.equal('source' in doc, false);
  assert.ok(!JSON.stringify(doc).includes('stencil-source:'));
  assert.equal(buildProjectFile(projectFileState(shedApp(A), { includeTheme: false })).source, A);
  assert.equal(projectFileState(shedApp('https://x/a.png'), { includeTheme: false }).source, 'https://x/a.png');
});

test('a hand-off of the open or a stored shed project carries no source', () => {
  const app = shedApp();
  assert.equal('source' in openInLaunchPayload(app), false, 'the open one');
  app.activeProjectId = 'p2';
  app.imageSource = A;
  assert.equal('source' in openInLaunchPayload(app, { id: 'p1' }), false, 'a stored one');
  app.activeProjectId = 'p1';
  assert.equal(openInLaunchPayload(app, { id: 'p2' }).source, A, 'one whose text survived');
});

test('stencil.current.source and a stored project\'s source read no source', () => {
  const app = withProjects();
  app._metas[1].source = REF;
  app._projects[2].payload.layout.imageSource = REF;
  app.imageSource = REF;
  const stencil = createStencil(app);
  assert.equal(stencil.current.source, '');
  assert.equal(stencil.getProjects({ archived: true }).find((p) => p.id === 2).source, '');
  app.imageSource = A;
  assert.equal(stencil.current.source, A);
});

test('the Links field shows no source for a shed project, and the URL otherwise', () => {
  const app = shedApp();
  StencilLinksModal.prototype.wire.call(null, app);
  document.getElementById('links-modal-overlay').classList.add('modal-open');
  const field = document.getElementById('links-source');
  field.value = 'stale';
  publish(EVENTS.registryChanged);
  assert.equal(field.value, '');
  app.imageSource = 'https://x/a.png';
  publish(EVENTS.registryChanged);
  assert.equal(field.value, 'https://x/a.png');
});

test('portableSource drops only the reference', () => {
  assert.equal(portableSource(REF), '');
  assert.equal(portableSource(A), A);
  assert.equal(portableSource(null), null);
});
