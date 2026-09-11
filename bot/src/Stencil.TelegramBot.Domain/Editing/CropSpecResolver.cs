using System.Globalization;

namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>A resolved crop rectangle in whole image pixels.</summary>
public sealed record CropRect(int X, int Y, int Width, int Height);

/// <summary>
/// C# port of the core's crop-spec resolution — the bot has no <c>core/</c> access, so like
/// mcp it ports the contract: <c>core/parse/cropSpec.cpp</c> (parse + resolve) +
/// <c>core/parse/lengthTokens.cpp</c> (length tokens), finished with
/// <c>core/cliApi.cpp stencil_cli_resolveCrop</c>'s integer rounding/clamping. Page metrics
/// are A4 oriented to the image, mirroring the CLI's <c>pipeline.namedPageForImage</c>.
/// </summary>
public static partial class CropSpecResolver
{
    private const double CmPerInch = 2.54;
    private const double A4ShortCm = 21.0;
    private const double A4LongCm = 29.7;

    private enum LengthKind { Px, Cm, Percent, Delta }

    private readonly record struct LengthToken(LengthKind Kind, double Value, bool FromEnd);

    private sealed record ParsedSpec(string? X1, string? X2, string? Y1, string? Y2, string? Aspect, bool Valid);

    /// <summary>
    /// Resolve a crop spec (e.g. <c>x1=10% x2=-10px</c>) against an image, or null when the
    /// spec is malformed, a token is unparseable, or the rect rounds to an empty area. Edges
    /// default to the full image; a single-axis spec derives the other axis from the page
    /// aspect (album = landscape proportion); an <c>aspect=W:H</c> key centre-fits the rect.
    /// </summary>
    public static CropRect? Resolve(string spec, double imageW, double imageH, bool album)
    {
        ParsedSpec parsed = Parse(spec);
        if (!parsed.Valid)
        {
            return null;
        }
        // A4 laid to match the image orientation, like the CLI pipeline's page derivation.
        double pageW = imageW > imageH ? A4LongCm : A4ShortCm;
        double pageH = imageW > imageH ? A4ShortCm : A4LongCm;
        double pxPerCmX = imageW / pageW;
        double pxPerCmY = imageH / pageH;

        bool ok = true;
        double Edge(string? token, double current, double lengthPx, double pxPerCm)
        {
            if (token is null)
            {
                return current;
            }
            double? resolved = ResolveAxisPx(token, lengthPx, pxPerCm, current);
            if (resolved is null)
            {
                ok = false;
                return current;
            }
            return resolved.Value;
        }

        double x1 = Edge(parsed.X1, 0.0, imageW, pxPerCmX);
        double x2 = Edge(parsed.X2, imageW, imageW, pxPerCmX);
        double y1 = Edge(parsed.Y1, 0.0, imageH, pxPerCmY);
        double y2 = Edge(parsed.Y2, imageH, imageH, pxPerCmY);
        if (!ok)
        {
            return null;
        }

        bool xGiven = parsed.X1 is not null || parsed.X2 is not null;
        bool yGiven = parsed.Y1 is not null || parsed.Y2 is not null;
        if (xGiven != yGiven)
        {
            double aspect = CropAspect(pageW, pageH, album);   // width / height
            if (aspect <= 0.0)
            {
                aspect = 1.0;
            }
            if (xGiven)
            {              // have a width -> derive the height
                y1 = 0.0;
                y2 = Math.Abs(x2 - x1) / aspect;
            }
            else
            {              // have a height -> derive the width
                x1 = 0.0;
                x2 = Math.Abs(y2 - y1) * aspect;
            }
        }

        double rectX = Math.Min(x1, x2);
        double rectY = Math.Min(y1, y2);
        double rectW = Math.Abs(x2 - x1);
        double rectH = Math.Abs(y2 - y1);

        // Optional aspect fit, applied to the resolved rect exactly like the core: a centered
        // shrink to W:H that never grows, keeps ≥ 1px, and holds fractional centres.
        if (parsed.Aspect is string aspectToken)
        {
            double ratio = ParseAspectRatio(aspectToken);   // width / height
            if (ratio <= 0.0)
            {
                return null;   // invalid aspect fails resolution
            }
            double fitW = rectW;
            double fitH = rectH;
            if (rectH * ratio <= rectW)
            {
                fitW = rectH * ratio;   // too wide  -> shrink the width
            }
            else
            {
                fitH = rectW / ratio;   // too tall  -> shrink the height
            }
            // Degenerate results keep at least 1px, but never grow past the resolved rect.
            fitW = Math.Min(rectW, Math.Max(fitW, 1.0));
            fitH = Math.Min(rectH, Math.Max(fitH, 1.0));
            rectX += (rectW - fitW) / 2.0;
            rectY += (rectH - fitH) / 2.0;
            rectW = fitW;
            rectH = fitH;
        }

        // Final integer rounding, as stencil_cli_resolveCrop does it.
        int iw = LRound(imageW);
        int ih = LRound(imageH);
        int x = Math.Clamp(LRound(rectX), 0, Math.Max(0, iw));
        int y = Math.Clamp(LRound(rectY), 0, Math.Max(0, ih));
        int w = Math.Clamp(LRound(rectW), 0, iw - x);
        int h = Math.Clamp(LRound(rectH), 0, ih - y);
        if (w <= 0 || h <= 0)
        {
            return null;   // an empty crop is not useful
        }
        return new CropRect(x, y, w, h);
    }

    /// <summary>Round half away from zero, like the C++ <c>lround</c>.</summary>
    private static int LRound(double value) => (int)Math.Round(value, MidpointRounding.AwayFromZero);

    /// <summary>Page aspect (width over height): album takes the landscape proportion.</summary>
    private static double CropAspect(double pageW, double pageH, bool album)
    {
        double lo = Math.Min(pageW, pageH);
        double hi = Math.Max(pageW, pageH);
        if (lo <= 0.0 || hi <= 0.0)
        {
            return 1.0;
        }
        return album ? hi / lo : lo / hi;
    }
}
