namespace Stencil.TelegramBot.Domain.Editing;

public readonly record struct ImageSize(int Width, int Height)
{
    public override string ToString() => $"{Width}x{Height}";
}
