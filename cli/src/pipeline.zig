//! End-to-end pipeline: acquire a source image (decode a file/URL, grab a video frame, or synthesise a
//! blank), then crop → rotate → filter → draw the layout, and encode the result. The C++ core does
//! every pixel/geometry transform; Zig owns I/O and codecs. The filter runs BEFORE the layout — it
//! belongs to the picture, the lines are an overlay — the layering the console, pystencil and the GUIs
//! share. The steps are `pub` building blocks (steps.zig); `run` (oneshot.zig) composes the one-shot.
const std = @import("std");
const confine = @import("confine.zig");
const page_mod = @import("page.zig");

const sources = @import("pipeline/sources.zig");
const steps = @import("pipeline/steps.zig");
const oneshot = @import("pipeline/oneshot.zig");

pub const Source = steps.Source;
pub const run = oneshot.run;

pub const acquireInput = steps.acquireInput;
pub const acquireBlank = sources.acquireBlank;
pub const applyCropSpec = steps.applyCropSpec;
pub const resolveCropSpec = steps.resolveCropSpec;
pub const cropToRect = steps.cropToRect;
pub const applyRotateBy = steps.applyRotateBy;
pub const loadLayoutDoc = steps.loadLayoutDoc;
pub const drawLayoutDoc = steps.drawLayoutDoc;
pub const applyFilterMode = steps.applyFilterMode;
pub const writeOutputLabeled = steps.writeOutputLabeled;
pub const loadLayoutBytes = sources.loadLayoutBytes;
pub const expandHome = sources.expandHome;

/// The `..` guard lives in confine.zig; re-exported for scrape.zig and the console's /save.
pub const hasParentTraversal = confine.hasParentTraversal;

// Page-format policy lives in page.zig; re-exported for the console's call sites.
pub const blankSizeFor = page_mod.blankSizeFor;
pub const effectivePageName = page_mod.effectivePageName;
pub const pageLabelAlloc = page_mod.pageLabelAlloc;

test {
    _ = sources;
    _ = steps;
    _ = oneshot;
}
