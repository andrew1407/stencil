//! The §13 descriptor shape: an op's capability tag, its record, and the two teeth that
//! keep the generated prompt honest — the forbidden-op boundary and the bullet censor.
const std = @import("std");
const opplan = @import("../opplan.zig");
const opSchema = @import("../opSchema.zig");

const Action = opplan.Action;

/// §13 capability tags: an op whose runtime capability is not wired into a build is excluded from
/// prompt assembly, so it falls to §1's unknown-op skip. The shipped console wires all of them.
pub const OpCapability = enum { theme, network, filesystem, clipboard };
pub const OpCaps = std.EnumSet(OpCapability);
pub const full_capabilities = OpCaps.initFull();

/// One §13 descriptor: an op's wire name, its `Action` tag, its prompt bullet(s) VERBATIM and its
/// capability. Key schema and variant gates live in opRegistry.json; a comptime check pins it to `Action`.
pub const OpDescriptor = struct {
    name: []const u8,
    tag: std.meta.Tag(Action),
    /// The §4 "Available ops" bullet. Null when a sibling's bullet covers the op (`redo` rides `undo`'s)
    /// or the op is parsed for §2 compatibility but never advertised (`reset`).
    bullet: ?[]const u8 = null,
    /// The console settings-block bullet (the CLI's §10-analog profile).
    console_bullet: ?[]const u8 = null,
    /// A console-block line riding AFTER the op bullets (crop's `album` spec key).
    console_addendum: ?[]const u8 = null,
    /// The runtime capability the op needs, when it needs one (§13 exclusion).
    capability: ?OpCapability = null,
};

/// §13 forbidden ops — the "never model-drivable" boundary, the registry's `forbidden.perSurface.cli`.
/// Two teeth: no descriptor may use one of these names, and `validateActions` rejects outright.
pub fn isForbiddenOp(op: []const u8) bool {
    return opSchema.get().isForbidden(op);
}

/// §13 prompt censor: patterns no generated bullet may match (`checkCensor`'s @compileError).
/// Deliberately NOT a bare "token" — the crop bullet's spec "tokens" are innocent.
pub const sensitive_patterns = [_][]const u8{
    "api key",  "api-key",       "apikey",  "api_key",
    "bearer",   "authorization", "secret",  "endpoint",
    "base url", "base_url",      "baseurl",
};

/// True when `text` matches a §13 sensitive pattern (case-insensitive). Works at
/// comptime (the generator's censor) and runtime (the parity test's assertions).
pub fn bulletIsSensitive(text: []const u8) bool {
    for (sensitive_patterns) |p| {
        if (std.ascii.findIgnoreCasePosLinear(text, 0, p) != null) return true;
    }
    return false;
}

pub fn checkCensor(comptime bullet: []const u8) []const u8 {
    if (bulletIsSensitive(bullet))
        @compileError("§13 censor: a registry bullet matches a sensitive pattern");
    return bullet;
}
