//! The op-plan actions that change the picture. Each one goes through the SAME session
//! operation the matching console command uses, so a plan and a typed command leave the
//! session in the same state.
const std = @import("std");
const image = @import("../../image.zig");
const pipeline = @import("../../pipeline.zig");
const logo = @import("../../logo.zig");
const core = @import("../../core.zig");
const llm = @import("../../llm.zig");
const layout_mod = @import("../../layout.zig");
const project = @import("../../project.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const handlers = @import("../handlers.zig");
const planOps = @import("planOps.zig");
const resolvePlanCrop = planOps.resolvePlanCrop;
const rotateQuarters = planOps.rotateQuarters;
const filterArg = planOps.filterArg;
const linesDoc = planOps.linesDoc;
const acquirePlanBlank = planOps.acquirePlanBlank;
const planSavePath = planOps.planSavePath;

/// The image-editing half of one op-plan action (§1–§2): crop, rotate, filter, layout, formula,
/// page, blank, image and save. Every other op is settings.zig's.
pub fn apply(
    session: *Session,
    io: std.Io,
    a: llm.Action,
    edited: *bool,
    frame_steps: *std.ArrayList(layout_mod.FrameStep),
    active: *?usize,
) bool {
    const gpa = session.gpa;
    switch (a) {
        .crop => |c| {
            const cur = session.current();
            const rect = resolvePlanCrop(gpa, cur.width, cur.height, c) orelse return false; // message printed
            session.applyCrop(rect) catch return false;
            frame_steps.append(gpa, .{ .crop = .{ .x = @floatFromInt(rect.x), .y = @floatFromInt(rect.y) } }) catch return false;
            edited.* = true;
            ui.ack(session, "cropped");
        },
        .rotate => |r| {
            // Record the PRE-rotate view dims the point mapping turns within.
            const cur = session.current().*;
            session.applyRotate(rotateQuarters(r.dir, r.times)) catch return false;
            frame_steps.append(gpa, .{ .rotate = .{
                .quarters = rotateQuarters(r.dir, r.times),
                .w = @floatFromInt(cur.width),
                .h = @floatFromInt(cur.height),
            } }) catch return false;
            edited.* = true;
            ui.ack(session, "rotated");
        },
        .filter => |f| {
            const mode = filterArg(f);
            if (!handlers.applyFilterArg(session, mode)) {
                logo.err("unknown filter \"{s}\"\n", .{mode});
                return false;
            }
            edited.* = true;
            ui.ack(session, mode);
        },
        .layout => |l| {
            // §4: an empty "lines" array REMOVES every drawn line — the /apply path's
            // replace mode over nothing, undoable like the draw it undoes.
            if (std.mem.eql(u8, l.lines_json, "[]")) {
                session.setLines("{\"lines\":[]}") catch return false;
                edited.* = true;
                ui.ack(session, "lines removed");
                return true;
            }
            // Re-map the snapshot-frame points through the plan's earlier crop/rotate and
            // clamp into the current view (identity steps still clamp).
            const cur = session.current().*;
            const remapped = layout_mod.remapLinesArrayAlloc(gpa, l.lines_json, frame_steps.items, cur.width, cur.height) catch return false;
            defer gpa.free(remapped);
            const doc = linesDoc(gpa, remapped) catch return false;
            defer gpa.free(doc);
            session.addLines(doc) catch return false;
            edited.* = true;
            ui.ack(session, "drawn");
        },
        .formula => |f| {
            // §2 enabled-form: the formulas on/off toggle (/formula on|off) — false
            // restores identity, keeping the expressions like the command does.
            if (f.enabled) |on| {
                session.setAllowFormulas(on);
                if (on) handlers.printFormula(session) else logo.print("formulas off (expressions kept)\n", .{});
                edited.* = true; // rides the saved/synced layout, like /formula
                return true;
            }
            const ok = session.setFormula(f.axis, f.expr) catch return false;
            if (!ok) {
                logo.err("invalid {c} formula: {s}\n", .{ f.axis, f.expr });
                return false;
            }
            edited.* = true; // rides the saved/synced layout, like /formula
            handlers.printFormula(session);
        },
        .page => |pf| {
            // §2 custom form: explicit cm dims — the /format custom path.
            if (pf.format.len == 0) {
                session.setPageSize("custom") catch return false;
                session.custom_page_w = pf.width;
                session.custom_page_h = pf.height;
                edited.* = true; // rides the saved/synced layout, like /format
                logo.print("page format set to custom ({d}×{d}cm)\n", .{ pf.width, pf.height });
                return true;
            }
            const name = core.canonicalPageFormat(pf.format) orelse {
                logo.err("unknown page format '{s}'\n", .{pf.format});
                return false;
            };
            session.setPageSize(name) catch return false;
            edited.* = true; // rides the saved/synced layout, like /format
            logo.print("page format set to {s}\n", .{name});
        },
        .blank => |b| {
            const blank = acquirePlanBlank(session, b, null) orelse return false; // message printed
            session.loadImage(blank.img, "blank", true, .png, null) catch return false;
            if (blank.custom_w > 0) {
                // §2 explicit cm dims: the page pick becomes custom, like /blank <w> <h>.
                session.setPageSize("custom") catch {};
                session.custom_page_w = blank.custom_w;
                session.custom_page_h = blank.custom_h;
            } else if (blank.page) |p| session.setPageSize(p) catch {};
            // A fresh picture starts a fresh frame — earlier crop/rotate steps don't apply.
            frame_steps.clearRetainingCapacity();
            edited.* = true;
            ui.redraw(session);
        },
        // §2.1: adopt the turn's Nth `/upload`ed image as the working image. An index
        // this turn cannot satisfy costs the ACTION only — the plan carries on.
        .image => |im| {
            const list = session.attachments.items;
            if (im.index == 0 or im.index > list.len) {
                logo.note("skipped switching to attached image {d} — this turn uploaded {d} image(s)\n", .{ im.index, list.len });
                return true;
            }
            const at = list[im.index - 1];
            var img = image.decode(gpa, at.bytes) catch {
                logo.note("skipped attached image {d} — '{s}' could not be decoded\n", .{ im.index, at.label });
                return true;
            };
            const bytes = gpa.dupe(u8, at.bytes) catch {
                img.deinit(gpa);
                return false;
            };
            // loadImage takes the pixels + a private copy of the encoded source (so the
            // attachment stays available for a later `image` op, or a second save).
            const label = at.label;
            session.loadImage(img, label, at.temp, at.fmt, bytes) catch return false;
            frame_steps.clearRetainingCapacity(); // a fresh picture = a fresh coordinate frame
            active.* = im.index;
            edited.* = true;
            ui.redraw(session);
        },
        // §2.1: persist the current image + layout through the same `/save <x>.stencil`
        // path the console command uses. Nothing loaded is a skipped action, not a stop.
        .save => |s| {
            if (!session.hasImage()) {
                logo.note("skipped save — no working image to save\n", .{});
                return true;
            }
            const path = planSavePath(session, io, s.name, active.*, s.path) orelse return true; // message printed
            defer gpa.free(path);
            // A destination naming an image format writes the picture (what /save does with
            // one); everything else is the project bundle.
            if (project.isStencilPath(path)) {
                handlers.saveProject(session, io, path) catch return false;
            } else {
                const page_label = session.pageFormatLabel() catch return false;
                defer gpa.free(page_label);
                pipeline.writeOutputLabeled(gpa, io, session.current().*, path, session.default_fmt, page_label) catch return true;
            }
        },
        else => unreachable, // plan.zig routes the settings ops to settings.zig
    }
    return true;
}
