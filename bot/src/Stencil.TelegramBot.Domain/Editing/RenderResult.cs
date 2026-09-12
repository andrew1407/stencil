namespace Stencil.TelegramBot.Domain.Editing;

// Parsed from the CLI's `wrote {path} ({w}x{h})` stderr line.
public sealed record RenderResult(string Path, int Width, int Height)
{
    public ImageSize Size => new(Width, Height);
}
