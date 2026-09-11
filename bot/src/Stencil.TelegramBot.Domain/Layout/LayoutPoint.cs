namespace Stencil.TelegramBot.Domain.Layout;

// Image-pixel vertex; the {x, y} shape shared with browser layout.js, cli/src/layout.zig,
// mcp/src/layout.rs and pystencil/layout.py.
public sealed record LayoutPoint(double X, double Y);
