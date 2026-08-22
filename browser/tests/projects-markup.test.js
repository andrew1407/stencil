import { test } from 'node:test';
import assert from 'node:assert';

// layout() transitively imports every ui component, including the projects modal.
import { layout } from '../js/ui/layout.js';
import { escapeHtml } from '../js/ui/base.js';

// The project-row / server-row badges interpolate server-provenance strings
// (meta.address, meta.serverUrl — user-typed connect URLs or server-returned
// metadata) into innerHTML via escapeHtml. A malicious server returning a crafted
// serverUrl must not inject markup into the projects modal.
test('escapeHtml neutralizes markup in server-provenance strings', () => {
  const evil = '"><img src=x onerror=alert(1)>';
  const out = escapeHtml(evil);
  assert.ok(!out.includes('<img'), 'angle brackets must be escaped');
  assert.ok(!out.includes('"'), 'quotes must be escaped');
  assert.strictEqual(out, '&quot;&gt;&lt;img src=x onerror=alert(1)&gt;');
  // A normal address is unchanged.
  assert.strictEqual(escapeHtml('https://host:8090'), 'https://host:8090');
  assert.strictEqual(escapeHtml(null), '');
});

const markup = layout();
const count = needle => markup.split(needle).length - 1;

const NEW_IDS = [
  'projects-btn', 'incognito-toggle', 'projects-modal-overlay', 'projects-close',
  'projects-list', 'projects-new-editor', 'projects-clear-all', 'projects-search',
  'projects-sort', 'projects-search-mode',
];

test('each new projects id appears exactly once', () => {
  for (const id of NEW_IDS) {
    assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
  }
});

test('#projects-list is empty/comment-only in static markup', () => {
  assert.ok(
    /<div class="settings-body" id="projects-list"><!-- filled by JS --><\/div>/.test(markup),
    'empty comment-only #projects-list present'
  );
});

test('no backticks or template interpolation leaked into markup', () => {
  assert.strictEqual(count('`'), 0, 'no backticks in markup');
  assert.strictEqual(count('${'), 0, 'no ${ in markup');
});

test('no runtime-only project row ids in static markup', () => {
  assert.strictEqual(count('project-row'), 0, 'no project-row markup statically');
  assert.strictEqual(count('project-thumb'), 0, 'no project-thumb markup statically');
});

// ── Filtering the projects list ─────────────────────────────────────────────
// Filter / sort / search-mode / name-search all re-list the same rows, so they all
// play the same symmetric transition (motion.js createFilterAnimator) instead of the
// silent rebuild they used to do. Pinned against the source: wiring the whole modal
// takes a project store, a tabs bus and a connection manager — the transition's own
// behaviour is unit-tested in motion.test.js, and end-to-end in connectModal.test.js.
import { readFileSync } from 'node:fs';

const projectsSrc = readFileSync(new URL('../js/ui/projectsModal.js', import.meta.url), 'utf8');

test('every projects filter control re-lists through the shared transition', () => {
  assert.match(projectsSrc, /import \{[^}]*createFilterAnimator[^}]*\} from '\.\/motion\.js'/,
    'the projects list uses the shared helper, not its own animation');
  assert.match(projectsSrc, /const runFilter = createFilterAnimator\(\{[\s\S]*?keys: \(\) => shownKeys,[\s\S]*?next: \(\) => rowPlan\(\)\.map/,
    'the "before" set is the last render’s keys, the "after" one the pending plan');
  // The four controls the user reported as un-animated.
  assert.match(projectsSrc, /attachSearchFilter\(search, \(\) => runFilter\(\)\)/, 'the name search');
  assert.match(projectsSrc, /filterEl\.addEventListener\('change', \(\) => \{ filterMode = filterEl\.value; runFilter\(\); \}\)/,
    'the kind filter');
  assert.match(projectsSrc, /sortEl\.addEventListener\('change', \(\) => \{ setSortMode\(sortEl\.value\); runFilter\(\); \}\)/,
    'the sort select');
  assert.match(projectsSrc, /searchModeEl\.addEventListener\('change'[\s\S]{0,120}runFilter\(\); \}\)/,
    'the search-mode select');
});

test('the filter transition and render() agree on the rows by construction', () => {
  // ONE plan builds the list and answers "what would this state show?" — two
  // independent lists would silently disagree about what a change moves.
  assert.match(projectsSrc, /const rowPlan = \(\) => \{/);
  assert.match(projectsSrc, /for \(const entry of rowPlan\(\)\) \{[\s\S]*?row\.dataset\.filterKey = entry\.key;/,
    'render() walks the plan and stamps each row with its key');
  assert.match(projectsSrc, /shownKeys\.push\(entry\.key\)/, 'and records what it listed');
});

test('a real removal keeps the destructive wipe — a filter is not a delete', () => {
  // The scatter/disintegrate effect stays on the removal paths only.
  assert.match(projectsSrc, /leaveThenRemove\(rowById\([\s\S]{0,20}\), \(\) => \{\}, scatterGridFor\(1\)\)/,
    'deleting a project still scatters');
  assert.ok(!/runFilter[\s\S]{0,200}scatterGridFor/.test(projectsSrc),
    'nothing on the filter path reaches for the dust');
});
