// The console's tables (js/console/shell/format.js): padded columns, the project rows local and
// remote, the active one marked, the long form with size, colour and date.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { padColumns, projectsRows, connectionsRows } from '../../../js/console/shell/format.js';

test('columns pad to the widest cell but the last', () => {
  assert.deepEqual(padColumns([['a', 'bb', 'c'], ['ddd', 'e', 'f']]), ['a    bb  c', 'ddd  e   f']);
});

test('projects: local rows then server rows, the active one starred, the long form adding columns', () => {
  const local = [{ id: 'p1', name: 'One', updatedAt: 0 }, { id: 'p2', name: 'Two', address: 'http://h', updatedAt: 0 }];
  const remote = [{ id: 'r1', name: 'Shared', serverUrl: 'http://s' }];
  const rows = projectsRows(local, remote, { activeId: 'p2' });
  assert.equal(rows.length, 3);
  assert.match(rows[0], /^ {2}One\s+local$/);
  assert.match(rows[1], /^\* Two\s+server http:\/\/h$/);
  assert.match(rows[2], /Shared\s+server http:\/\/s$/);
  const long = projectsRows([{ id: 'p1', name: 'One', imageWidth: 10, imageHeight: 5, color: '#ff0000', updatedAt: Date.UTC(2026, 0, 2, 3, 4) }], [], { long: true });
  assert.match(long[0], /10x5\s+#ff0000\s+2026-01-02 03:04$/);
  assert.deepEqual(projectsRows([], []), ['(no projects)']);
  assert.deepEqual(connectionsRows([]), ['(no server connections)']);
  assert.deepEqual(connectionsRows(['http://h']), ['  http://h']);
});
