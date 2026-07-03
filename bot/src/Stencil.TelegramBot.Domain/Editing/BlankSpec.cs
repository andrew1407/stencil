namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// A blank-canvas request, matching the CLI's <c>--blank [format] [w h] [color]</c> grammar.
/// Provide <see cref="Width"/> and <see cref="Height"/> together, or a named ISO
/// <see cref="Page"/> format (e.g. <c>B5</c>), or neither for the core's default A4 @ 96 dpi
/// page — a page format and explicit dims are mutually exclusive. <see cref="Color"/> is a
/// CSS name or <c>#hex</c> (default white when null).
/// </summary>
public sealed record BlankSpec(int? Width = null, int? Height = null, string? Color = null, string? Page = null);
