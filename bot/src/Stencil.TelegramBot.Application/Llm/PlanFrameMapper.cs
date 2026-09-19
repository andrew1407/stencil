using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

// Re-maps plan layout coordinates from the snapshot frame the model saw into the frame after the plan's
// earlier crop/rotate steps (§1): subtract crop origins, turn through quarter rotations, clamp.
public sealed class PlanFrameMapper
{
    private abstract record Step;

    private sealed record CropStep(double Dx, double Dy) : Step;

    // With the PRE-rotate frame dims they turn within.
    private sealed record RotateStep(int Quarters, double W, double H) : Step;

    private readonly List<Step> _steps = new();

    public double Width { get; private set; }

    public double Height { get; private set; }

    public PlanFrameMapper(double width, double height)
    {
        Width = width;
        Height = height;
    }

    // The frame the session currently renders to: the stored crop on the base dims, swapped on an
    // odd rotation. Nothing else in an EditState resizes the picture.
    public static PlanFrameMapper ForSession(UserSession session)
    {
        double w = session.OriginalWidth;
        double h = session.OriginalHeight;
        if (session.Edits.CropSpec is string spec
            && CropSpecResolver.Resolve(spec, w, h, session.Edits.Album) is CropRect rect)
        {
            (w, h) = (rect.Width, rect.Height);
        }
        if (session.Edits.Rotate % 2 != 0)
        {
            (w, h) = (h, w);
        }
        return new PlanFrameMapper(w, h);
    }

    public void Reset(double width, double height)
    {
        _steps.Clear();
        Width = width;
        Height = height;
    }

    // Plan crops never use album; an unresolvable spec records nothing — the render surfaces that
    // error.
    public void RecordCrop(string spec)
    {
        if (CropSpecResolver.Resolve(spec, Width, Height, album: false) is not CropRect rect)
        {
            return;
        }
        _steps.Add(new CropStep(rect.X, rect.Y));
        Width = rect.Width;
        Height = rect.Height;
    }

    // Odd totals swap the tracked dims.
    public void RecordRotate(int quarterTurns)
    {
        int quarters = ((quarterTurns % 4) + 4) % 4;
        if (quarters == 0)
        {
            return;
        }
        _steps.Add(new RotateStep(quarters, Width, Height));
        if (quarters % 2 != 0)
        {
            (Width, Height) = (Height, Width);
        }
    }

    public LayoutPoint Map(LayoutPoint point)
    {
        double x = point.X;
        double y = point.Y;
        foreach (Step step in _steps)
        {
            switch (step)
            {
                case CropStep crop:
                    x -= crop.Dx;
                    y -= crop.Dy;
                    break;
                case RotateStep rotate:
                    double h = rotate.H;
                    double w = rotate.W;
                    for (int i = 0; i < rotate.Quarters; i++)
                    {
                        // One CW quarter turn: (x, y) -> (h - y, x), the continuous twin of core
                        // rotateImageRGBA's (h-1-y, x).
                        (x, y) = (h - y, x);
                        (w, h) = (h, w);
                    }
                    break;
            }
        }
        return Clamp(new LayoutPoint(x, y), Width, Height);
    }

    public IReadOnlyList<LayoutLine> MapLines(IReadOnlyList<LayoutLine> lines) =>
        lines.Select(line => line with { Points = line.Points.Select(Map).ToList() }).ToList();

    // The browser's mapPointsHome convention.
    public static LayoutPoint Clamp(LayoutPoint point, double width, double height) => new(
        Math.Clamp(point.X, 0.0, Math.Max(0.0, width)),
        Math.Clamp(point.Y, 0.0, Math.Max(0.0, height)));

    public static IReadOnlyList<LayoutLine> ClampLines(IReadOnlyList<LayoutLine> lines, double width, double height) =>
        lines.Select(line => line with { Points = line.Points.Select(p => Clamp(p, width, height)).ToList() }).ToList();
}
