namespace Stencil.TelegramBot.Domain.Layout;

/// <summary>
/// A single vertex in image-pixel space.
/// </summary>
/// <remarks>
/// Mirrors the <c>{x, y}</c> shape shared across the Stencil front-ends
/// (browser <c>layout.js</c> ← <c>cli/src/layout.zig</c> ← <c>core/raster</c>,
/// and the ports in <c>mcp/src/layout.rs</c> / <c>pystencil/layout.py</c>).
/// </remarks>
public sealed record LayoutPoint(double X, double Y);
