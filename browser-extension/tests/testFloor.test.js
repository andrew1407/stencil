// Test-count floor for browser-extension/, the twin of browser/tests/testFloor.test.js: the
// suite re-runs itself once and reads the runner's own total, since green says nothing about
// how many tests ran. STENCIL_TEST_COUNT_RUN marks the inner run, so it never nests.
import test from 'node:test';
import assert from 'node:assert';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

// A floor, not a pin: raise it when the suite grows a lot; additions must never trip it.
const TEST_FLOOR = 1580;
const INNER_RUN = 'STENCIL_TEST_COUNT_RUN';

test('test-count floor: the suite still discovers and runs its whole tree',
  { skip: process.env[INNER_RUN] ? 'inner count run' : false }, () => {
    // NODE_TEST_CONTEXT is this process's own runner marker; inherited, it mutes the inner run.
    const innerEnv = { ...process.env, [INNER_RUN]: '1' };
    delete innerEnv.NODE_TEST_CONTEXT;
    const inner = spawnSync(process.execPath, ['--test', '--test-reporter=tap'], {
      cwd: fileURLToPath(new URL('../', import.meta.url)), encoding: 'utf8',
      maxBuffer: 256 * 1024 * 1024, env: innerEnv,
    });
    const total = /^# tests (\d+)$/m.exec(inner.stdout || '');
    assert.ok(total, `the inner runner printed no test total (exit ${inner.status})`);
    const count = Number(total[1]);
    assert.ok(count >= TEST_FLOOR,
      `browser-extension suite collapsed to ${count} tests, floor is ${TEST_FLOOR}`);
  });
