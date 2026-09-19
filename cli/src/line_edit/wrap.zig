//! Where a long input sits on screen: how many rows it wraps onto, which slice of it a row
//! shows, and how Up/Down walk those rows before handing back to history.
const std = @import("std");
const testing = std.testing;

// command history (a small ring of owned strings, oldest first)

/// The tallest the wrapped input block can grow (screen.maxPromptRows clamps to this too).
pub const max_prompt_rows = 8;

/// How many rows `len` bytes occupy when the first row holds `first` and the rest hold `cols`.
/// Always at least 1 (an empty line still owns its row).
pub fn wrappedRows(len: usize, first: usize, cols: usize) usize {
    if (len <= first) return 1;
    const rest = len - first;
    return 1 + (rest + cols - 1) / cols;
}

/// The slice of `line` shown on wrapped row `idx` (0-based).
pub fn rowSlice(line: []const u8, idx: usize, first: usize, cols: usize) []const u8 {
    if (idx == 0) return line[0..@min(first, line.len)];
    const start = first + (idx - 1) * cols;
    if (start >= line.len) return line[line.len..];
    return line[start..@min(start + cols, line.len)];
}

/// Byte offset into `line` where wrapped row `idx` begins.
fn rowStart(idx: usize, first: usize, cols: usize) usize {
    return if (idx == 0) 0 else first + (idx - 1) * cols;
}

/// Where the cursor lands after moving one wrapped row up (`up`) or down, keeping the SCREEN column —
/// row 0 is indented by the prompt. Null when there is no such row, so Up/Down fall back to history.
pub fn rowMove(len: usize, pos: usize, prompt_len: usize, first: usize, cols: usize, up: bool) ?usize {
    const rows = wrappedRows(len, first, cols);
    if (rows == 1) return null;
    const cur = wrappedRows(pos + 1, first, cols) - 1;
    if (up and cur == 0) return null;
    if (!up and cur + 1 >= rows) return null;
    const target = if (up) cur - 1 else cur + 1;
    const screen_col = (pos - rowStart(cur, first, cols)) + if (cur == 0) prompt_len else 0;
    const want = screen_col -| (if (target == 0) prompt_len else 0);
    const width = if (target == 0) first else cols;
    return @min(rowStart(target, first, cols) + @min(want, width), len);
}

test "rowMove: Up/Down walk a wrapped line's rows, then hand back to history" {
    // Geometry: prompt "> " (2 cols), a 10-col first row, 12-col continuation rows. The line
    // is 30 bytes, so it occupies rows [0..10), [10..22), [22..30) — three rows.
    const len: usize = 30;
    const first: usize = 10;
    const cols: usize = 12;
    const pl: usize = 2;

    // From the top row there is nothing above: null = "do the history thing instead".
    try testing.expectEqual(@as(?usize, null), rowMove(len, 3, pl, first, cols, true));
    // …and from the last row there is nothing below.
    try testing.expectEqual(@as(?usize, null), rowMove(len, 27, pl, first, cols, false));
    // A line that fits one row keeps Up/Down as previous/next command, wherever the cursor is.
    try testing.expectEqual(@as(?usize, null), rowMove(8, 4, pl, first, cols, true));
    try testing.expectEqual(@as(?usize, null), rowMove(8, 4, pl, first, cols, false));

    // Down from row 0 keeps the SCREEN column: offset 3 sits at column 2+3=5, and row 1 has
    // no prompt in front of it, so the cursor lands 5 bytes into it.
    try testing.expectEqual(@as(?usize, 15), rowMove(len, 3, pl, first, cols, false));
    // Up from there returns to where it started (the move is symmetric).
    try testing.expectEqual(@as(?usize, 3), rowMove(len, 15, pl, first, cols, true));
    // Down from row 1 to row 2, same column, no prompt on either.
    try testing.expectEqual(@as(?usize, 27), rowMove(len, 15, pl, first, cols, false));
    // A column past the end of the target row clamps to the end of the line, never past it.
    try testing.expectEqual(@as(?usize, 30), rowMove(len, 21, pl, first, cols, false));
    // Coming back up onto row 0, the prompt's own columns are not walkable: column 1 is offset 0.
    try testing.expectEqual(@as(?usize, 0), rowMove(len, 10, pl, first, cols, true));
}

test "wrappedRows / rowSlice: the input flows onto as many rows as it needs" {
    // The first row is shorter (the prompt sits on it); an empty line still owns one row.
    try testing.expectEqual(@as(usize, 1), wrappedRows(0, 10, 20));
    try testing.expectEqual(@as(usize, 1), wrappedRows(10, 10, 20));
    try testing.expectEqual(@as(usize, 2), wrappedRows(11, 10, 20));
    try testing.expectEqual(@as(usize, 2), wrappedRows(30, 10, 20));
    try testing.expectEqual(@as(usize, 3), wrappedRows(31, 10, 20));

    const line = "0123456789abcdefghijklmnopqrstuvwxyz";
    try testing.expectEqualStrings("0123456789", rowSlice(line, 0, 10, 8));
    try testing.expectEqualStrings("abcdefgh", rowSlice(line, 1, 10, 8));
    try testing.expectEqualStrings("ijklmnop", rowSlice(line, 2, 10, 8));
    // A row past the end is empty rather than out of bounds.
    try testing.expectEqualStrings("", rowSlice(line, 9, 10, 8));
}
