// stencil.project lifecycle (js/console/stencilApi.js): renew/open/close, the expiration
// facade and the incognito project's synthetic name.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp, lastCall, withProjects } from './helpers/stencilApiRig.js';

test('project.renew/open/close route to app and return the project for chaining', () => {
  const app = withProjects();
  const stencil = createStencil(app);
  const p = stencil.getProjectByName('beta');

  assert.equal(p.renew(), p);
  assert.deepEqual(lastCall(app, 'renewProject'), ['renewProject', 2]);
  assert.equal(p.open(), p);
  assert.deepEqual(lastCall(app, 'switchToProject'), ['switchToProject', 2]);
  p.close({ fully: true });
  assert.deepEqual(lastCall(app, 'closeProject'), ['closeProject', 2, { fully: true }]);
});

test('project expiration facade: expiresAt/refreshPeriod/autoRefresh/keepForever route to app', () => {
  const app = withProjects();
  const stencil = createStencil(app);
  const p = stencil.getProjectByName('beta');

  const future = Date.now() + 10 * 24 * 60 * 60 * 1000;
  p.expiresAt = future;
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { expiresAt: future }]);
  assert.throws(() => { p.expiresAt = Date.now() - 1000; }, /past/);
  assert.throws(() => { p.expiresAt = 'not-a-date'; }, /Invalid expiration/);

  p.refreshPeriod = 'month';
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { refreshPeriod: 'month' }]);
  assert.equal(p.refreshPeriod, 'month');
  assert.throws(() => { p.refreshPeriod = 'decade'; }, /Invalid refresh period/);

  p.autoRefresh = false;
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { autoRefresh: false }]);
  assert.equal(p.autoRefresh, false);

  assert.equal(p.keepForever(), p);
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { expiresAt: 0 }]);
});

test('project.expire(spec) parses a duration → setProjectExpiration; expirationDate get/set + top-level expire', () => {
  const app = withProjects();
  const stencil = createStencil(app);
  const p = stencil.getProjectByName('beta');
  const DAY = 24 * 60 * 60 * 1000;

  // No-arg → the formats help string (mutates nothing).
  const help = p.expire();
  assert.equal(typeof help, 'string');
  assert.match(help, /fortnight/);

  // A free-form duration resolves to now + span and routes through the shared setter.
  const before = Date.now();
  assert.equal(p.expire('months 3'), p); // chainable
  const call = lastCall(app, 'setProjectExpiration');
  assert.equal(call[1], 2);
  assert.ok(call[2].expiresAt >= before + 90 * DAY && call[2].expiresAt <= Date.now() + 90 * DAY, 'expiry ~90d out');

  // 'off' keeps it forever; an invalid spec throws.
  p.expire('off');
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { expiresAt: 0 }]);
  assert.throws(() => p.expire('banana'), /Invalid duration/);

  // expirationDate setter accepts a Date and routes exactly like expiresAt.
  const future = Date.now() + 5 * DAY;
  p.expirationDate = new Date(future);
  assert.deepEqual(lastCall(app, 'setProjectExpiration'), ['setProjectExpiration', 2, { expiresAt: future }]);
  assert.throws(() => { p.expirationDate = new Date(Date.now() - 1000); }, /past/);

  // Top-level stencil.expire(): no arg prints formats regardless of an active project.
  assert.match(stencil.expire(), /fortnight/);
});

test('incognito project: synthetic name + mutating links/name throw', () => {
  const app = makeApp({ storage: { ...makeApp().storage, incognito: true } });
  const stencil = createStencil(app);

  const inc = stencil.current;   // active id null + incognito storage → incognito project
  assert.equal(inc.incognito, true);
  assert.equal(inc.name, 'Incognito (unsaved)');
  assert.throws(() => { inc.name = 'x'; }, /Cannot rename an incognito/);
  assert.throws(() => { inc.source = 'http://x'; }, /Cannot set links on an incognito/);
});
