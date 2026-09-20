//! `--script-emit out.pystc`: the script as Python over pystencil's Editor. Every call is
//! the twin of pystencil/pystencil/editor/script.py, and the block loop the twin of
//! pystencil/pystencil/script.py, so an emitted file batches exactly as `--script` does.
const std = @import("std");

const scriptCore = @import("../../scriptCore.zig");
const common = @import("common.zig");

pub const display = "python";

const preamble =
    \\import sys
    \\
    \\from pystencil import Editor
    \\from pystencil.layout import Line, Point
    \\from pystencil.scriptpaths import base_name, expand_source, resolve_target, save_format
    \\
    \\PX_PER_CM = 96.0 / 2.54
    \\
    \\
    \\def _len(token, extent):
    \\  """One .stc length token against an axis; the twin of core/parse/lengthTokens.cpp."""
    \\  text = str(token).strip().lower()
    \\  from_end = text.startswith("-")
    \\  body = (text[1:] if from_end else text).strip()
    \\  digits = 0
    \\  while digits < len(body) and (body[digits].isdigit() or body[digits] == "."): digits += 1
    \\  if digits == 0: return 0.0
    \\  value = float(body[:digits])
    \\  unit = body[digits:].strip()
    \\  if unit == "": return -value if from_end else value
    \\  if unit == "%": px = value / 100.0 * extent
    \\  elif unit == "cm": px = value * PX_PER_CM
    \\  elif unit == "mm": px = value / 10.0 * PX_PER_CM
    \\  elif unit == "in": px = value * 2.54 * PX_PER_CM
    \\  else: px = value
    \\  return extent - px if from_end else px
    \\
    \\
    \\def _pt(editor, x, y):
    \\  width, height = editor.image_size
    \\  return Point(_len(x, width), _len(y, height))
    \\
    \\
    \\def _save(editor, target, source):
    \\  name = base_name(source)
    \\  ext = name.rsplit(".", 1)[-1].lower() if "." in name else ""
    \\  path = resolve_target(target, source, None, save_format(ext))
    \\  editor.save(path)
    \\  return path
    \\
    \\
    \\def main(source=None):
    \\
;

fn quote(out: *std.Io.Writer, text: []const u8) !void {
    try common.writeQuoted(out, '"', text);
}

fn crop(out: *std.Io.Writer, script: scriptCore.Script, i: u32) !void {
    const spec = common.cropOf(script, i);
    try out.writeAll("editor.crop(\"");
    var wrote = false;
    for (common.Crop.keys, spec.toks) |key, tok| {
        if (tok.len == 0) continue;
        try out.print("{s}{s}={s}", .{ if (wrote) " " else "", key, tok });
        wrote = true;
    }
    if (spec.aspect.len > 0) try out.print("{s}aspect={s}", .{ if (wrote) " " else "", spec.aspect });
    try out.print("\"{s})\n", .{if (spec.album) ", album=True" else ""});
}

fn shape(out: *std.Io.Writer, script: scriptCore.Script, i: u32, o: scriptCore.Op) !void {
    try out.writeAll("editor.draw([Line(points=[");
    var k: u32 = 0;
    while (k < common.pointCount(script, i, o)) : (k += 1) {
        const pt = common.pointAt(script, i, o, k);
        if (k > 0) try out.writeAll(", ");
        try out.writeAll("_pt(editor, ");
        try quote(out, pt.x);
        try out.writeAll(", ");
        try quote(out, pt.y);
        try out.writeAll(")");
    }
    try out.writeAll("], color=");
    try quote(out, script.opStr(i, 0));
    try out.writeAll(", style=");
    const style = script.opStr(i, 1);
    try quote(out, if (style.len > 0) style else "solid");
    try out.writeAll(", fill_color=");
    try quote(out, script.opStr(i, 2));
    try out.writeAll(", point_color=");
    try quote(out, script.opStr(i, 3));
    try out.writeAll(", thickness=");
    try common.writeNumber(out, script.opNum(i, 0) orelse 2);
    try out.writeAll(", point_size=");
    try common.writeNumber(out, script.opNum(i, 1) orelse 4);
    try out.print(", locked={s})])\n", .{if (o.kind == .rect) "True" else "False"});
}

fn op(out: *std.Io.Writer, script: scriptCore.Script, i: u32, o: scriptCore.Op, src: []const u8, bad: *common.Refusal) !void {
    switch (o.kind) {
        .open => {}, // the block header already opened the editor
        // The same named deviation the pystencil runner reports: no decoder in a stdlib-only package.
        .frame => return bad.set(o.line, o.col, "@frame needs a video decoder — use the CLI", .{}),
        .crop => try crop(out, script, i),
        .filter => {
            const mode = script.opStr(i, 0);
            try out.writeAll("editor.apply_filter(");
            try quote(out, if (std.mem.eql(u8, mode, "custom")) script.opStr(i, 1) else mode);
            try out.writeAll(")\n");
        },
        .line, .rect => try shape(out, script, i, o),
        .layout => {
            try out.writeAll("editor.draw(");
            try quote(out, script.opStr(i, 0));
            try out.print(", combine={s})\n", .{if (std.mem.eql(u8, script.opStr(i, 1), "replace")) "False" else "True"});
        },
        .save => {
            try out.writeAll("_save(editor, ");
            try quote(out, script.opStr(i, 0));
            try out.print(", {s})\n", .{src});
        },
        .undo, .redo => try out.print(
            "for _ in range({d}): editor.{s}()\n",
            .{ common.steps(script, i), if (o.kind == .redo) "redo" else "undo" },
        ),
    }
}

const project_block =
    \\  if source is None: raise SystemExit("this script has no @source block — pass an image path")
    \\  editor = Editor()
    \\  editor.load(source, source=source)
    \\
;

const footer =
    \\
    \\
    \\if __name__ == "__main__":
    \\  main(sys.argv[1] if len(sys.argv) > 1 else None)
    \\
;

/// The whole file: the banner, the helpers, then `main()` — one loop per @source block over
/// the inputs it names, exactly the inputs `--script` would open.
pub fn write(out: *std.Io.Writer, script: scriptCore.Script, label: []const u8, bad: *common.Refusal) !void {
    try out.print("#!/usr/bin/env python3\n# Generated from {s} by `stencil --script-emit`; edit the .stc, not this file.\n\n", .{label});
    try out.writeAll(preamble);

    var b: u32 = 0;
    var wrote_body = false;
    while (b < script.blockCount()) : (b += 1) {
        const block = script.block(b) orelse continue;
        const sourced = block.kind != .project;
        if (sourced) {
            try out.writeAll("  for path in expand_source(");
            try quote(out, block.source);
            try out.print(", \"{s}\"):\n    editor = Editor()\n    editor.load(path, source=path)\n", .{@tagName(block.kind)});
        } else try out.writeAll(project_block);
        wrote_body = true;

        var i: u32 = block.op_start;
        while (i < block.op_start + block.op_count) : (i += 1) {
            const o = script.op(i) orelse continue;
            if (o.kind == .open) continue;
            try out.writeAll(if (sourced) "    " else "  ");
            try op(out, script, i, o, if (sourced) "path" else "source", bad);
        }
    }
    if (!wrote_body) try out.writeAll("  return\n");
    try out.writeAll(footer);
}
