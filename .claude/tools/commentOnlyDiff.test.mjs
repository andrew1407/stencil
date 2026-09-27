// commentOnlyDiff's self-test under node --test, so CI runs it with the other tool suites.
// Run: node --test .claude/tools/commentOnlyDiff.test.mjs
import { test } from 'node:test';
import { selfTest } from './commentOnlyDiff.mjs';

test('commentOnlyDiff self-test', selfTest);
