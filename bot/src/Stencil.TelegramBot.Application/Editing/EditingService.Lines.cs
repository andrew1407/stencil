using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

// EditingService — the pen defaults and the drawn lines. Class doc lives in EditingService.cs.
public sealed partial class EditingService
{
    /// <inheritdoc />
    public async Task<UserSession> ConfigurePenAsync(long userId, string? color, double? thickness, double? pointSize, string? style, string? fill, CancellationToken ct = default)
    {
        // The CLI SKIPS a colour it can't parse, so a typo would silently paint the default
        // instead of failing. Reject it here, for /color, /fill and the §10 lineStyle op alike.
        RejectUnparseableColor(color, "colour");
        RejectUnparseableColor(NormalizeFill(fill), "fill");
        var session = await _store.GetAsync(userId, ct);
        var pen = session.Edits.Pen;
        var updatedPen = pen with
        {
            Color = color ?? pen.Color,
            Thickness = thickness ?? pen.Thickness,
            PointSize = pointSize ?? pen.PointSize,
            Style = style ?? pen.Style,
            FillColor = NormalizeFill(fill) ?? pen.FillColor,
        };
        var updated = session with { Edits = session.Edits with { Pen = updatedPen } };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public Task<UserSession> AddLineAsync(long userId, IReadOnlyList<LayoutPoint> points, bool closed, CancellationToken ct = default) =>
        ApplyEditAsync(userId, session =>
        {
            var pen = session.Edits.Pen;
            var pts = points.ToList();
            if (closed && pts.Count >= 1)
            {
                var first = pts[0];
                var last = pts[^1];
                if (last.X != first.X || last.Y != first.Y)
                {
                    pts.Add(first);
                }
            }
            var line = new LayoutLine
            {
                Points = pts,
                Color = pen.Color,
                Thickness = pen.Thickness,
                PointSize = pen.PointSize,
                Style = pen.Style,
                Locked = closed,
                FillColor = closed ? pen.FillColor : LayoutLine.DefaultFillColor,
            };
            var layout = session.Edits.Layout ?? EmptyLayout(session);
            var lines = layout.Lines.Append(line).ToList();
            var updatedLayout = layout with
            {
                Lines = lines,
                ImageWidth = session.OriginalWidth,
                ImageHeight = session.OriginalHeight,
            };
            return session.Edits with { Layout = updatedLayout };
        }, ct);

    /// <inheritdoc />
    public async Task<UserSession> RemoveLastLineAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var layout = session.Edits.Layout;
        if (layout is null || layout.Lines.Count == 0)
        {
            return session;
        }
        var lines = layout.Lines.Take(layout.Lines.Count - 1).ToList();
        var updatedLayout = lines.Count == 0 ? null : layout with { Lines = lines };
        var updated = EditSessions.WithHistory(session, session.Edits with { Layout = updatedLayout });
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public async Task<UserSession> ClearLinesAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.Edits.Layout is null)
        {
            return session;
        }
        var updated = EditSessions.WithHistory(session, session.Edits with { Layout = null });
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <summary>A fresh empty layout carrying the working image's dimensions.</summary>
    private static StencilLayout EmptyLayout(UserSession session) =>
        new()
        {
            ImageWidth = session.OriginalWidth,
            ImageHeight = session.OriginalHeight,
            Lines = [],
        };

    /// <summary>Throw when a caller-supplied colour is one <c>parseColor</c> would drop.</summary>
    private static void RejectUnparseableColor(string? spec, string label)
    {
        if (spec is not null && !ColorSpec.IsValid(spec))
        {
            throw new InvalidOperationException(
                $"'{spec}' isn't a {label} I understand — use #rgb/#rrggbb, a CSS colour name, or transparent.");
        }
    }

    /// <summary>null keeps the fill; <c>none</c>/<c>clear</c>/blank clears it; else the colour.</summary>
    private static string? NormalizeFill(string? fill)
    {
        if (fill is null)
        {
            return null;
        }
        if (string.IsNullOrWhiteSpace(fill) || fill is "none" or "clear" or "transparent")
        {
            return LayoutLine.DefaultFillColor;
        }
        return fill;
    }

}
