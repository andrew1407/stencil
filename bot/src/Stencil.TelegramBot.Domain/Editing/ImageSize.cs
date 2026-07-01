namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>Pixel dimensions of an image.</summary>
public readonly record struct ImageSize(int Width, int Height)
{
    public override string ToString() => $"{Width}x{Height}";
}
