//! The formula half of the core bridge (core/parse/formulaParser.hpp): the two context-free
//! entry points, and the two that put the named values — the other axis, PAGE_*, IMAGE_* — in
//! reach. core.zig re-exports all four, so every caller still goes through it.
const std = @import("std");
const testing = std.testing;

const c = @cImport({
    @cInclude("core/cliApi.h");
});

/// The named values a formula may read besides its own axis (core/parse/formulaContext.hpp).
/// `unset` means "not supplied": that name stays unknown, so the formula reads as invalid and
/// apply returns its input. Page fields are cm, image fields pixels.
pub const FormulaCtx = struct {
    pub const unset = std.math.nan(f64);

    x: f64 = unset,
    y: f64 = unset,
    page_w_cm: f64 = unset,
    page_h_cm: f64 = unset,
    image_w: f64 = unset,
    image_h: f64 = unset,
    unit: ?[:0]const u8 = null, // "cm" | "in"; null reads as cm

    fn unitPtr(self: FormulaCtx) [*c]const u8 {
        return if (self.unit) |u| u.ptr else null;
    }
};

/// Validate a single-variable formula (`var_name` is 'x' or 'y'). Empty = valid (identity).
pub fn validateFormula(expr: [:0]const u8, var_name: u8) bool {
    return c.stencil_cli_validateFormula(expr.ptr, @as(c_int, var_name)) != 0;
}

/// Apply a formula to `value` ('x'/'y' variable). Identity when disabled, empty, or invalid.
pub fn applyFormula(expr: [:0]const u8, var_name: u8, value: f64, allow: bool) f64 {
    return c.stencil_cli_applyFormula(expr.ptr, @as(c_int, var_name), value, @intFromBool(allow));
}

/// Validate `expr` with the named values in reach. An unbound axis probes at 1, as it always has.
pub fn validateFormulaCtx(expr: [:0]const u8, ctx: FormulaCtx) bool {
    return c.stencil_cli_validateFormulaCtx(expr.ptr, ctx.x, ctx.y, ctx.page_w_cm, ctx.page_h_cm,
        ctx.image_w, ctx.image_h, ctx.unitPtr()) != 0;
}

/// Apply `expr` to `value` with the named values in reach; `value` still binds `var_name` and is
/// the identity fallback.
pub fn applyFormulaCtx(expr: [:0]const u8, var_name: u8, value: f64, allow: bool, ctx: FormulaCtx) f64 {
    return c.stencil_cli_applyFormulaCtx(expr.ptr, @as(c_int, var_name), value, @intFromBool(allow),
        ctx.x, ctx.y, ctx.page_w_cm, ctx.page_h_cm, ctx.image_w, ctx.image_h, ctx.unitPtr());
}

test "formula validate + apply through the ABI" {
    try testing.expect(validateFormula("x*2", 'x'));
    try testing.expect(validateFormula("", 'x')); // empty = identity = valid
    try testing.expect(!validateFormula("foo(x)", 'x')); // unknown ident = invalid
    try testing.expectEqual(@as(f64, 20), applyFormula("x*2", 'x', 10, true));
    try testing.expectEqual(@as(f64, 10), applyFormula("x*2", 'x', 10, false)); // disabled = identity
    try testing.expectEqual(@as(f64, 10), applyFormula("bad(", 'x', 10, true)); // invalid = identity
}

test "formula named values through the context ABI" {
    const a4 = FormulaCtx{ .page_w_cm = 21.0, .page_h_cm = 29.7, .image_w = 600, .image_h = 400 };
    try testing.expectEqual(@as(f64, 9), applyFormulaCtx("9", 'x', 3, true, a4)); // constant only
    var live = a4;
    live.x = 4;
    live.y = 2;
    try testing.expectEqual(@as(f64, 2), applyFormulaCtx("x / y", 'x', 4, true, live)); // cross-axis
    try testing.expectEqual(@as(f64, 600), applyFormulaCtx("IMAGE_WIDTH", 'x', 1, true, a4));
    try testing.expectEqual(@as(f64, 21), applyFormulaCtx("PAGE_WIDTH", 'x', 1, true, a4)); // null = cm
    var inches = a4;
    inches.unit = "in";
    try testing.expectApproxEqAbs(@as(f64, 21.0 / 2.54), applyFormulaCtx("PAGE_WIDTH", 'x', 1, true, inches), 1e-12);
    try testing.expectApproxEqAbs(@as(f64, 29.7), applyFormulaCtx("PAGE_HEIGHT_CM", 'x', 1, true, inches), 1e-12);
    try testing.expect(validateFormulaCtx("PAGE_WIDTH + PAGE_HEIGHT - x / 2", a4));
    // No image open: IMAGE_* is unsupplied, so the expression is invalid and the raw value stands.
    const blank = FormulaCtx{ .page_w_cm = 21.0, .page_h_cm = 29.7 };
    try testing.expect(!validateFormulaCtx("IMAGE_WIDTH", blank));
    try testing.expectEqual(@as(f64, 42), applyFormulaCtx("IMAGE_WIDTH", 'x', 42, true, blank));
}
