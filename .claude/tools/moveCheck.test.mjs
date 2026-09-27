// moveCheck's self-test under node --test, so CI runs it with the other tool suites: the unit
// diff and the string-aware scanner commentOnlyDiff and commentPaths share.
// Run: node --test .claude/tools/moveCheck.test.mjs
import { test } from 'node:test';
import { selfTest } from './moveCheck/selfTest.mjs';

test('moveCheck self-test', selfTest);
