// stencil.project metadata (js/console/stencilApi.js): lookup by name, renames, colour,
// description, keywords and the blank-image colour rule.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp, lastCall, withProjects } from './helpers/stencilApiRig.js';

// ── Projects ──────────────────────────────────────────────────────────────────────

test('getProjectByName matches case-insensitively; current wraps the active project', () => {
  const app = withProjects();
  const stencil = createStencil(app);

  assert.equal(stencil.getProjectByName('alpha').id, 1);
  assert.equal(stencil.getProjectByName('  BETA  ').id, 2);
  assert.equal(stencil.getProjectByName('nope'), null);
  assert.equal(stencil.current.id, 1);
});

test('project.name setter validates empty + duplicate names and routes to renameProject', () => {
  const app = withProjects();
  app.renameProject = (id, name) => { app.calls.push(['renameProject', id, name]); return true; };
  const stencil = createStencil(app);

  const p = stencil.getProjectByName('alpha');
  assert.throws(() => { p.name = '   '; }, /cannot be empty/);
  assert.throws(() => { p.name = 'Beta'; }, /already exists/);   // collides with id 2
  p.name = 'Gamma';
  assert.deepEqual(lastCall(app, 'renameProject'), ['renameProject', 1, 'Gamma']);
});

test('stencil.projectColor reads the active meta colour and validates on set', () => {
  const app = withProjects();
  app._metas = [{ id: 1, name: 'Alpha', color: '#ec4899' }, { id: 2, name: 'Beta' }];
  const stencil = createStencil(app);

  // Getter: active project's colour, or '' when unset.
  assert.equal(stencil.projectColor, '#ec4899');
  app._metas[0].color = '';
  assert.equal(stencil.projectColor, '');

  // Setter: a valid hex routes to setProjectColor; junk throws; '' clears.
  stencil.projectColor = '#0EA5E9';
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 1, '#0EA5E9']);
  stencil.projectColor = '';
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 1, '']);
  assert.throws(() => { stencil.projectColor = 'not-a-color'; }, /Invalid project color/);
});

test('stencil.projectColor throws with no active project', () => {
  const app = makeApp();   // activeProjectId null
  const stencil = createStencil(app);
  assert.equal(stencil.projectColor, '');
  assert.throws(() => { stencil.projectColor = '#fff'; }, /No active project/);
});

test('stencil.description / stencil.keywords read the active meta and route to the app setters', () => {
  const app = withProjects();
  app._metas = [{ id: 1, name: 'Alpha', description: 'Site plan', keywords: ['plan', 'north'] }, { id: 2, name: 'Beta' }];
  const stencil = createStencil(app);

  assert.equal(stencil.description, 'Site plan');
  assert.deepEqual(stencil.keywords, ['plan', 'north']);
  stencil.keywords.push('x');   // a copy, not the stored array
  assert.deepEqual(stencil.keywords, ['plan', 'north']);

  stencil.description = 'North wing';
  assert.deepEqual(lastCall(app, 'setProjectDescription'), ['setProjectDescription', 1, 'North wing']);
  stencil.description = null;   // clears
  assert.deepEqual(lastCall(app, 'setProjectDescription'), ['setProjectDescription', 1, '']);
  // Keywords: ONE per array entry; a string splits on commas and newlines, never on a
  // space — the coerce.js splitter stencil.current.keywords comes through too.
  stencil.keywords = ['a', 'b'];
  assert.deepEqual(lastCall(app, 'setProjectKeywords'), ['setProjectKeywords', 1, ['a', 'b']]);
  stencil.keywords = 'one, two three';
  assert.deepEqual(lastCall(app, 'setProjectKeywords'), ['setProjectKeywords', 1, ['one', 'two three']]);
  stencil.keywords = ['kitchen  remodel', '', ' floor plan '];
  assert.deepEqual(lastCall(app, 'setProjectKeywords'), ['setProjectKeywords', 1, ['kitchen remodel', 'floor plan']]);
  // …and the project wrapper's own setter is the same rule, not a second one.
  stencil.current.keywords = 'one, two three';
  assert.deepEqual(lastCall(app, 'setProjectKeywords'), ['setProjectKeywords', 1, ['one', 'two three']]);
});

test('stencil.description / stencil.keywords throw with no active project', () => {
  const app = makeApp();   // activeProjectId null
  const stencil = createStencil(app);
  assert.equal(stencil.description, '');
  assert.deepEqual(stencil.keywords, []);
  assert.throws(() => { stencil.description = 'x'; }, /No active project/);
  assert.throws(() => { stencil.keywords = ['x']; }, /No active project/);
});

test('project.color get/set validates hex and routes to setProjectColor', () => {
  const app = withProjects();
  app._metas = [{ id: 1, name: 'Alpha', color: '#16a34a' }, { id: 2, name: 'Beta' }];
  const stencil = createStencil(app);

  const p = stencil.getProjectByName('alpha');
  assert.equal(p.color, '#16a34a');
  p.color = '#abc';                       // normalises in DrawingApp; facade passes the trimmed value
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 1, '#abc']);
  p.color = '';                           // clear is allowed
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 1, '']);
  assert.throws(() => { p.color = 'zzz'; }, /Invalid project color/);
});

test('project.blank/blankColor: read-only blank flag, colour routes to setProjectBlankColor', () => {
  const app = withProjects();
  app._metas = [
    { id: 1, name: 'Alpha', blank: true, blankColor: '#00aaff' },
    { id: 2, name: 'Beta' },   // ordinary image project (not blank)
  ];
  const stencil = createStencil(app);

  const blankP = stencil.getProjectByName('alpha');
  assert.equal(blankP.blank, true);
  assert.equal(blankP.blankColor, '#00aaff');
  blankP.blankColor = '#ff0000';          // valid hex → routes to setProjectBlankColor
  assert.deepEqual(lastCall(app, 'setProjectBlankColor'), ['setProjectBlankColor', 1, '#ff0000']);
  assert.throws(() => { blankP.blankColor = 'zzz'; }, /Invalid blank color/);

  // A non-blank project: blank=false, blankColor=null, and setting throws (nothing to recolour).
  const imgP = stencil.getProjectByName('beta');
  assert.equal(imgP.blank, false);
  assert.equal(imgP.blankColor, null);
  assert.throws(() => { imgP.blankColor = '#ffffff'; }, /not a blank image/);
});
