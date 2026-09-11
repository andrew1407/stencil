using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>
/// Re-maps op-plan layout coordinates from the snapshot frame the model was shown into the
/// frame produced by the plan's earlier crop/rotate actions (<c>llm-contract.md</c> §1):
/// subtract each resolved crop origin, turn points through quarter rotations, then clamp
/// into the tracked bounds. Tracks the working frame's dimensions as steps accumulate.
/// </summary>
public sealed class PlanFrameMapper
{
    private abstract record Step;

    private sealed record CropStep(double Dx, double Dy) : Step;

    /// <summary>Clockwise quarter turns with the PRE-rotate frame dims they turn within.</summary>
    private sealed record RotateStep(int Quarters, double W, double H) : Step;

    private readonly List<Step> _steps = new();

    public double Width { get; private set; }

    public double Height { get; private set; }

    public PlanFrameMapper(double width, double height)
    {
        Width = width;
        Height = height;
    }

    /// <summary>Drop all steps and restart at a fresh frame (a blank/frame replaced the image).</summary>
    public void Reset(double width, double height)
    {
        _steps.Clear();
        Width = width;
        Height = height;
    }

    /// <summary>
    /// Record a crop, resolving the spec against the CURRENT tracked dims (plan crops never
    /// use album). An unresolvable spec records nothing — the render surfaces that error.
    /// </summary>
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

    /// <summary>Record quarter turns (positive = clockwise); odd totals swap the tracked dims.</summary>
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

    /// <summary>Map one snapshot-frame point through the recorded steps, clamped into bounds.</summary>
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
                        // One CW quarter turn: (x, y) -> (h - y, x), then the dims swap —
                        // the continuous twin of core rotateImageRGBA's (h-1-y, x) pixels.
                        (x, y) = (h - y, x);
                        (w, h) = (h, w);
                    }
                    break;
            }
        }
        return Clamp(new LayoutPoint(x, y), Width, Height);
    }

    /// <summary>Map every line's points, keeping all non-point fields unchanged.</summary>
    public IReadOnlyList<LayoutLine> MapLines(IReadOnlyList<LayoutLine> lines) =>
        lines.Select(line => line with { Points = line.Points.Select(Map).ToList() }).ToList();

    /// <summary>Clamp a point into [0,w] x [0,h] — the browser's <c>mapPointsHome</c> convention.</summary>
    public static LayoutPoint Clamp(LayoutPoint point, double width, double height) => new(
        Math.Clamp(point.X, 0.0, Math.Max(0.0, width)),
        Math.Clamp(point.Y, 0.0, Math.Max(0.0, height)));

    /// <summary>Clamp every line's points into bounds, keeping all non-point fields unchanged.</summary>
    public static IReadOnlyList<LayoutLine> ClampLines(IReadOnlyList<LayoutLine> lines, double width, double height) =>
        lines.Select(line => line with { Points = line.Points.Select(p => Clamp(p, width, height)).ToList() }).ToList();
}
