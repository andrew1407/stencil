//! `--script-emit out.stcjs`: the script as JavaScript over the page's `window.stencil`
//! facade. Every call is the twin of browser/js/console/scriptRunner.js, so an emitted
//! file and a script run in the app take the same facade path.
const std = @import("std");

const net = @import("../../net.zig");
const scriptCore = @import("../core.zig");
const common = @import("common.zig");

pub const display = "javascript";

const preamble =
    \\const PX_PER_CM = 96 / 2.54;
    \\
    \\// One .stc length token against an axis; the twin of core/parse/lengthTokens.cpp.
    \\const _len = (token, extent) => {
    \\  const text = String(token).trim().toLowerCase();
    \\  const fromEnd = text.startsWith('-');
    \\  const body = (fromEnd ? text.slice(1) : text).trim();
    \\  const value = parseFloat(body);
    \\  if (!Number.isFinite(value)) return 0;
    \\  const unit = body.replace(/^[0-9.]+/, '').trim();
    \\  if (unit === '') return fromEnd ? -value : value;
    \\  let px = value;
    \\  if (unit === '%') px = (value / 100) * extent;
    \\  else if (unit === 'cm') px = value * PX_PER_CM;
    \\  else if (unit === 'mm') px = (value / 10) * PX_PER_CM;
    \\  else if (unit === 'in') px = value * 2.54 * PX_PER_CM;
    \\  return fromEnd ? extent - px : px;
    \\};
    \\
    \\const _pt = (x, y) => {
    \\  const size = stencil.imageSize || {};
    \\  return { x: _len(x, size.width || 0), y: _len(y, size.height || 0) };
    \\};
    \\
    \\const _layout = async (url) => {
    \\  const res = await fetch(url);
    \\  if (!res.ok) throw new Error(`could not load the layout '${url}'`);
    \\  return res.json();
    \\};
    \\
    \\// A named @save renames the open project first; an incognito one has no name to take.
    \\const _save = async (name) => {
    \\  const project = stencil.current;
    \\  if (name && project && !project.incognito && project.name !== name) project.name = name;
    \\  await stencil.save();
    \\};
    \\
    \\
;

fn quote(out: *std.Io.Writer, text: []const u8) !void {
    try common.writeQuoted(out, '\'', text);
}

fn crop(out: *std.Io.Writer, script: scriptCore.Script, i: u32) !void {
    const spec = common.cropOf(script, i);
    try out.writeAll("stencil.crop({");
    var wrote = false;
    for (common.Crop.keys, spec.toks) |key, tok| {
        if (tok.len == 0) continue;
        try out.print("{s} {s}: ", .{ if (wrote) "," else "", key });
        try quote(out, tok);
        wrote = true;
    }
    if (spec.aspect.len > 0) {
        try out.print("{s} aspect: ", .{if (wrote) "," else ""});
        try quote(out, spec.aspect);
        wrote = true;
    }
    if (spec.album) try out.print("{s} album: true", .{if (wrote) "," else ""});
    try out.writeAll(if (wrote) " });\n" else "});\n");
}

fn shape(out: *std.Io.Writer, script: scriptCore.Script, i: u32, o: scriptCore.Op) !void {
    try out.writeAll("stencil.setLines([{ points: [");
    var k: u32 = 0;
    while (k < common.pointCount(script, i, o)) : (k += 1) {
        const pt = common.pointAt(script, i, o, k);
        if (k > 0) try out.writeAll(", ");
        try out.writeAll("_pt(");
        try quote(out, pt.x);
        try out.writeAll(", ");
        try quote(out, pt.y);
        try out.writeAll(")");
    }
    try out.writeAll("], color: ");
    try quote(out, script.opStr(i, 0));
    try out.writeAll(", style: ");
    try quote(out, script.opStr(i, 1));
    try out.writeAll(", fillColor: ");
    try quote(out, script.opStr(i, 2));
    try out.writeAll(", pointColor: ");
    try quote(out, script.opStr(i, 3));
    try out.writeAll(", thickness: ");
    try common.writeNumber(out, script.opNum(i, 0) orelse 2);
    try out.writeAll(", pointSize: ");
    try common.writeNumber(out, script.opNum(i, 1) orelse 4);
    try out.print(", locked: {s} }}], {{ mode: 'combine' }});\n", .{if (o.kind == .rect) "true" else "false"});
}

fn filter(out: *std.Io.Writer, script: scriptCore.Script, i: u32) !void {
    const mode = script.opStr(i, 0);
    if (std.mem.eql(u8, mode, "custom")) {
        try out.writeAll("stencil.apply({ filter: 'custom', filterColor: ");
        try quote(out, script.opStr(i, 1));
        try out.writeAll(" });\n");
        return;
    }
    try out.writeAll("stencil.apply({ filter: ");
    try quote(out, mode);
    try out.writeAll(" });\n");
}

fn op(out: *std.Io.Writer, script: scriptCore.Script, i: u32, o: scriptCore.Op, source: []const u8, bad: *common.Refusal) !void {
    switch (o.kind) {
        .open => {
            // The browser has no filesystem: only a URL can be opened (stc-contract §10).
            if (!net.isUrl(source)) return bad.set(o.line, o.col, "the browser can only open a URL — '{s}' is a local path", .{source});
            try out.writeAll("await stencil.load(");
            try quote(out, source);
            try out.writeAll(");\n");
        },
        .frame => {
            if (source.len == 0) return bad.set(o.line, o.col, "@frame needs a @source first", .{});
            try out.writeAll("await stencil.load(");
            try quote(out, source);
            try out.writeAll(", { frame: ");
            try common.writeNumber(out, script.opNum(i, 0) orelse 0);
            try out.writeAll(" });\n");
        },
        .crop => try crop(out, script, i),
        .filter => try filter(out, script, i),
        .line, .rect => try shape(out, script, i, o),
        .layout => {
            const src = script.opStr(i, 0);
            if (!net.isUrl(src)) return bad.set(o.line, o.col, "@layout needs a URL in the browser — '{s}' is a local path", .{src});
            try out.writeAll("stencil.applyLayout(await _layout(");
            try quote(out, src);
            try out.print("), {{ mode: '{s}' }});\n", .{if (std.mem.eql(u8, script.opStr(i, 1), "replace")) "replace" else "combine"});
        },
        .save => {
            try out.writeAll("await _save(");
            try quote(out, script.opStr(i, 0));
            try out.writeAll(");\n");
        },
        .undo, .redo => try out.print(
            "for (let i = 0; i < {d}; i += 1) stencil.{s}();\n",
            .{ common.steps(script, i), if (o.kind == .redo) "redo" else "undo" },
        ),
    }
}

/// The whole file: the banner, the helpers, then one statement per op in script order.
pub fn write(out: *std.Io.Writer, script: scriptCore.Script, label: []const u8, bad: *common.Refusal) !void {
    try out.print("// Generated from {s} by `stencil --script-emit`; edit the .stc, not this file.\n", .{label});
    try out.writeAll("// Runs where the app does: its console, or VS Code's \"Run in Stencil Web Console\".\n\n");
    try out.writeAll(preamble);

    var b: u32 = 0;
    while (b < script.blockCount()) : (b += 1) {
        const block = script.block(b) orelse continue;
        var i: u32 = block.op_start;
        while (i < block.op_start + block.op_count) : (i += 1) {
            const o = script.op(i) orelse continue;
            try op(out, script, i, o, block.source, bad);
        }
    }
}
