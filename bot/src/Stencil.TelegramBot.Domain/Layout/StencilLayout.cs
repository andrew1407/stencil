namespace Stencil.TelegramBot.Domain.Layout;

/// <summary>
/// A full layout payload: optional source dimensions and a baked-in filter, plus the
/// polylines to draw. This is the JSON the CLI's <c>--layout</c> flag consumes and the
/// shape every front-end exports (see <see cref="LayoutLine"/>).
/// </summary>
/// <remarks>
/// Coordinates are image pixels. <see cref="Filter"/> is <c>bw</c>/<c>sepia</c> or a
/// CSS colour / <c>#hex</c> for a duotone tint; a top-level CLI <c>--filter</c> overrides
/// it. <see cref="ImageWidth"/>/<see cref="ImageHeight"/> are advisory.
/// </remarks>
public sealed record StencilLayout
{
    public double? ImageWidth { get; init; }
    public double? ImageHeight { get; init; }
    public string? Filter { get; init; }
    public IReadOnlyList<LayoutLine> Lines { get; init; } = [];
}
