namespace Stencil.TelegramBot.Domain.Editing;

// The CLI's --blank [format] [w h] [color] grammar: Width+Height together, OR a named ISO Page
// (B5), OR neither for the core's default A4 @ 96 dpi — page and dims are mutually exclusive.
// Color is a CSS name or #hex; null is white.
public sealed record BlankSpec(int? Width = null, int? Height = null, string? Color = null, string? Page = null);
