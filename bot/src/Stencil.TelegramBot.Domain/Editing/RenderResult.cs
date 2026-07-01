namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// A successful CLI render: the resolved output path and the final image dimensions.
/// Parsed from the CLI's <c>wrote {path} ({w}x{h})</c> stderr line (see
/// <c>mcp/src/outcome.rs</c>).
/// </summary>
public sealed record RenderResult(string Path, int Width, int Height)
{
    public ImageSize Size => new(Width, Height);
}
