import { test } from 'node:test';
import assert from 'node:assert';

// layout() transitively imports every ui component, including the projects modal.
import { layout } from '../../../js/ui/layout.js';
import { escapeHtml } from '../../../js/ui/base.js';
import { escapeHtml as tipEscape } from '../../../js/ui/tip/content.js';
import { escapeHtml as oneEscape } from '../../../js/ui/escapeHtml.js';
import { projectsModalSource } from '../../helpers/projectsModalSource.js';

// The row badges interpolate server-provenance strings (meta.address, meta.serverUrl) into
// innerHTML via escapeHtml: a malicious server must not inject markup into the projects modal.
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

// Filter, sort, search-mode and name-search all re-list the same rows, so all play the one symmetric
// transition (motion.js createFilterAnimator); pinned against the source, its behaviour in motion.test.js.

const projectsSrc = projectsModalSource();

test('every projects filter control re-lists through the shared transition', () => {
  assert.match(projectsSrc, /import \{[^}]*createFilterAnimator[^}]*\} from '[^']*motion\.js'/,
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

test('what a removal REVEALS arrives, it does not simply appear', () => {
  // Only the rows the settle ADDED materialize — veiled behind their own motes, the removal played
  // backwards — on the desktop's own ROW_ARRIVE_MS, twin of ListFilterFade::dustRowIn.
  assert.match(projectsSrc, /const before = \[\.\.\.shownKeys\];/,
    'the settle remembers what the list showed before the removal');
  assert.match(projectsSrc, /const \{ entering \} = filterDelta\(before, shownKeys\);[\s\S]{0,400}materialize\(el, \{ \.\.\.rowDustGrid\(entering\.length, i\), dustMs: ROW_ARRIVE_MS \}\)/,
    'and materializes the keys it ADDED — the shared delta and grain, on the arrival clock');
  assert.match(projectsSrc, /import \{[^}]*materialize[^}]*\} from '[^']*motion\.js'/,
    'through the shared helper, not an animation of its own');
});

test('the row and its arrival land together, once the ash has thinned', () => {
  // The settle waits the falling leg out, THEN renders and materializes in one turn: rendering as the
  // collapse ends drops the arrival inside a full-strength scatter, where its own motes are invisible.
  assert.match(projectsSrc, /await new Promise\(\(r\) => setTimeout\(r, ROW_ARRIVE_DELAY_MS\)\);\n\s+render\(\);/,
    'the ash thins first, then the rebuild');
  assert.match(projectsSrc, /materialize\(el[\s\S]{0,400}await new Promise\(\(r\) => setTimeout\(r, Math\.max\(0, wipeDurationMs\(\) - ROW_ARRIVE_DELAY_MS\)\)\);/,
    'and only the remainder of the wipe trails the arrival');
});

test('a real removal keeps the destructive wipe — a filter is not a delete', () => {
  // The scatter stays on the removal paths only, on the ITEM clock: a project card is read, not merely
  // noticed, so its wipe runs half again as long as the connections list's (motion.js ITEM_DUST_MS).
  assert.match(projectsSrc, /leaveThenRemove\(rowById\([\s\S]{0,20}\), \(\) => \{\}, rowLeaveDust\(1, 0, ITEM_DUST_MS\)\)/,
    'deleting a project still scatters');
  assert.ok(!/runFilter[\s\S]{0,200}scatterGridFor/.test(projectsSrc),
    'nothing on the filter path reaches for the dust');
});

// ONE escaper: base.js and content.js re-export js/ui/escapeHtml.js rather than each
// keeping a copy (browser-extension/tests/portParity.test.js pins the extension's port of it).
test('escapeHtml is a single implementation, re-exported', () => {
  assert.strictEqual(escapeHtml, oneEscape);
  assert.strictEqual(tipEscape, oneEscape);
  assert.strictEqual(escapeHtml(`it's <b>"&"</b>`), 'it&#39;s &lt;b&gt;&quot;&amp;&quot;&lt;/b&gt;');
});
