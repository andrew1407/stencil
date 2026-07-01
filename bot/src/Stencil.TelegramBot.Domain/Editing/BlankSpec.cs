namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// A blank-canvas request. Provide <see cref="Width"/> and <see cref="Height"/> together,
/// or leave both null for the core's default A4 @ 96 dpi page. <see cref="Color"/> is a CSS
/// name or <c>#hex</c> (default white when null), matching the CLI's <c>--blank</c> flag.
/// </summary>
public sealed record BlankSpec(int? Width = null, int? Height = null, string? Color = null);
