// The rendered connection rows (js/ui/connectModal.js): the batch bar and its selection, the
// golden admin row with its invite, and the three-way credential filter's subsets.
import { test } from 'node:test';
import assert from 'node:assert';
import { matchesConnFilter } from '../js/ui/connect/connectModal.js';
import { COMPONENTS_CSS } from './helpers/css.js';
import { conn, openModal, rows, rowUrls, hasClass, find, markup, count } from './helpers/connectModalRig.js';

// A connection whose stored credential can mint session tokens (credentialKind 'admin')
// wears the --remote-gold outline server projects wear, plus a three-way view filter.

test('matchesConnFilter: all keeps everything, the other two split on credentialKind', () => {
  const admin = { credentialKind: 'admin' };
  const session = { credentialKind: '' };
  assert.strictEqual(matchesConnFilter(admin, 'all'), true);
  assert.strictEqual(matchesConnFilter(session, 'all'), true);
  assert.strictEqual(matchesConnFilter(null, 'all'), true);
  assert.strictEqual(matchesConnFilter(admin, 'admin'), true);
  assert.strictEqual(matchesConnFilter(session, 'admin'), false);
  assert.strictEqual(matchesConnFilter(null, 'admin'), false, 'an unknown row is not admin');
  assert.strictEqual(matchesConnFilter(admin, 'non-admin'), false);
  assert.strictEqual(matchesConnFilter(session, 'non-admin'), true);
});

test('the filter select is in the static markup, all three states, defaulting to All', () => {
  assert.strictEqual(count('id="connect-filter"'), 1);
  const select = /<select id="connect-filter"[\s\S]*?<\/select>/.exec(markup)?.[0] ?? '';
  for (const opt of ['<option value="all">All</option>', '<option value="admin">Admin</option>',
    '<option value="non-admin">Non-admin</option>']) {
    assert.ok(select.includes(opt), `${opt} present`);
  }
  // First option wins with nothing preselected — All is the default, and the choice
  // is view state: no `selected` attribute, nothing read back from storage.
  assert.match(select, /<select[^>]*>\s*<option value="all">/);
  assert.ok(!select.includes('selected'), 'no filter is persisted/preselected');
});


// The batch bar stays while the list has rows (it hosts Select all); only the count and the
// selection-only actions come and go, so the list never jumps.

test('the batch bar stays while rows exist; count + actions ride the selection', () => {
  const { doc, list } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const bar = doc.getElementById('connect-batch-bar');
  const count = doc.getElementById('connect-batch-count');
  const group = doc.getElementById('connect-batch-selected');
  const selectAll = doc.getElementById('connect-select-all');
  assert.notStrictEqual(bar.style.display, 'none', 'the bar is up with nothing checked…');
  assert.strictEqual(count.style.display, 'none', '…but the count waits for a selection');
  assert.strictEqual(group.style.display, 'none', '…and so do the selection actions');
  assert.notStrictEqual(selectAll.style.display, 'none', 'Select all is what the bar holds');
  assert.strictEqual(selectAll.querySelector('span').textContent, 'Select all');
  const cb = find(rows(list)[0], 'connect-select');
  cb.checked = true;
  cb.dispatch('change');
  assert.notStrictEqual(count.style.display, 'none', 'one checked → the count appears');
  assert.strictEqual(count.textContent, '1 selected');
  assert.notStrictEqual(group.style.display, 'none', '…with the selection actions');
  assert.ok(hasClass(rows(list)[0], 'connect-selected'));
});

test('Select all takes the filtered view; Deselect all clears the lot', () => {
  const { doc, list, filter, selectAllGlyph } = openModal([
    conn('http://adm:1', 'admin'), conn('http://plain:2', ''), conn('http://adm:3', 'admin'),
  ]);
  const selectAll = doc.getElementById('connect-select-all');
  const count = doc.getElementById('connect-batch-count');
  filter.value = 'admin';
  filter.dispatch('change');
  selectAll.dispatch('click');
  assert.strictEqual(count.textContent, '2 selected', 'only the rows on view are swept up');
  assert.strictEqual(selectAll.querySelector('span').textContent, 'Deselect all');
  assert.strictEqual(selectAllGlyph(), 'x', 'Deselect all wears a cross, not the check (user decision)');
  assert.ok(rows(list).every((r) => hasClass(r, 'connect-selected')));
  filter.value = 'all';
  filter.dispatch('change');
  const plain = rows(list).find((r) => r.dataset.url === 'http://plain:2');
  assert.ok(!hasClass(plain, 'connect-selected'), 'the filtered-out row was never checked');
  assert.strictEqual(selectAll.querySelector('span').textContent, 'Select all',
    'not everything on view is checked any more');
  selectAll.dispatch('click');
  assert.strictEqual(count.textContent, '3 selected');
  selectAll.dispatch('click');
  assert.strictEqual(count.textContent, '0 selected', 'Deselect all clears the selection');
  assert.strictEqual(count.style.display, 'none');
  assert.strictEqual(selectAllGlyph(), 'check', '…and the glyph is a check again');
  assert.ok(rows(list).every((r) => !hasClass(r, 'connect-selected')));
});

test('a selection is one visit\u2019s: reopening the modal starts unchecked', () => {
  const { doc, list, modal } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const cb = find(rows(list)[0], 'connect-select');
  cb.checked = true;
  cb.dispatch('change');
  assert.strictEqual(doc.getElementById('connect-batch-count').textContent, '1 selected');
  modal.close();
  modal.open();
  assert.strictEqual(doc.getElementById('connect-batch-count').textContent, '0 selected',
    'nothing carried over (user decision — projects parity)');
  assert.ok(rows(list).every((r) => !hasClass(r, 'connect-selected')));
  assert.ok(rows(list).every((r) => !find(r, 'connect-select').checked));
});

test('with no rows on view the bar goes too', () => {
  const { doc, filter } = openModal([conn('http://plain:2', '')]);
  const bar = doc.getElementById('connect-batch-bar');
  filter.value = 'admin';
  filter.dispatch('change');
  assert.strictEqual(bar.style.display, 'none', 'nothing to select → no bar');
  assert.strictEqual(openModal([]).doc.getElementById('connect-batch-bar').style.display, 'none');
});

test('an admin connection renders golden + badged; a session one does not', () => {
  const { list } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const [admin, plain] = rows(list);
  assert.ok(hasClass(admin, 'connect-admin'), 'the admin row wears the golden-outline class');
  assert.ok(!hasClass(plain, 'connect-admin'), 'a session-token row stays neutral');
  const badge = find(admin, 'connect-admin-badge');
  assert.ok(badge, 'the admin row carries the gold marker');
  assert.match(badge.dataset.title, /mint session tokens/i, 'the marker says what admin means');
  assert.strictEqual(find(plain, 'connect-admin-badge'), null);
});

test('the invite button is admin-only', () => {
  const { list } = openModal([conn('http://adm:1', 'admin'), conn('http://plain:2', '')]);
  const [admin, plain] = rows(list);
  assert.ok(find(admin, 'connect-invite'), 'only an admin credential can mint an invite');
  assert.strictEqual(find(plain, 'connect-invite'), null,
    'a session token would 401 — no button to press');
});

test('the three-way filter shows the right subset of connections', () => {
  const { list, filter } = openModal([
    conn('http://adm:1', 'admin'), conn('http://plain:2', ''), conn('http://adm:3', 'admin'),
  ]);
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://plain:2', 'http://adm:3'], 'All by default');
  filter.value = 'admin';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://adm:3']);
  filter.value = 'non-admin';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://plain:2']);
  filter.value = 'all';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), ['http://adm:1', 'http://plain:2', 'http://adm:3']);
});

test('a filter matching nothing shows its own empty state, not "No servers connected."', () => {
  const { list, filter } = openModal([conn('http://plain:2', '')]);
  filter.value = 'admin';
  filter.dispatch('change');
  assert.deepEqual(rowUrls(list), []);
  const empty = list.children.find((c) => c.classList?.contains('info-empty'));
  assert.ok(empty, 'the filtered-empty list still says something');
  assert.strictEqual(empty.textContent, 'No connections match this filter.');
});

test('with nothing connected the plain empty state is kept', () => {
  const { list } = openModal([]);
  const empty = list.children.find((c) => c.classList?.contains('info-empty'));
  assert.strictEqual(empty.textContent, 'No servers connected.');
});

test('components.css: the admin row reuses the server-projects gold', () => {
  const css = COMPONENTS_CSS;
  assert.match(css, /\.connect-row\.connect-admin \{[^}]*border-color: var\(--remote-gold\)/,
    'the same custom property .project-row.project-remote uses');
  assert.match(css, /\.connect-admin-badge \{[^}]*color: var\(--remote-gold\)/);
  // Declared before the transient row states, so selected/expired still win the border.
  assert.ok(css.indexOf('.connect-row.connect-admin') < css.indexOf('.connect-row.connect-expired'));
});
