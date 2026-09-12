namespace Stencil.TelegramBot.Domain.Editing;

// The CLI's --blank [format] [w h] [color] grammar: Width+Height, OR a named ISO Page, OR neither
// (the core's A4 @ 96 dpi default). Color is a CSS name or #hex; null is white.
public sealed record BlankSpec(int? Width = null, int? Height = null, string? Color = null, string? Page = null);
