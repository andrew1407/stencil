// columnIndex, the per-line table the paint pass maps token byte columns through: it must agree
// with the walk-from-line-start unitIndexOfColumn at every column, inside a character or past the end.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { columnIndex, unitIndexOfColumn, utf8Length } from '../../../js/core/script/types.js';

const UNICODE_LINE = '@save "café 日本😀.png" 10px # é \ud800 x \udc00 😀😀';

test('columnIndex agrees with unitIndexOfColumn at every column of a unicode-rich line', () => {
  for (const line of [UNICODE_LINE, '', 'ascii only', '😀']) {
    const index = columnIndex(line);
    assert.equal(index.bytes, utf8Length(line));
    for (let col = -2; col <= index.bytes + 4; col += 1)
      assert.equal(index.unitOf(col), unitIndexOfColumn(line, col), `${JSON.stringify(line)} col ${col}`);
  }
});
