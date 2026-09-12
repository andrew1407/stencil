using System.Globalization;

namespace Stencil.TelegramBot.Domain.Editing;

public sealed record CropRect(int X, int Y, int Width, int Height);

// Port of core/parse/cropSpec.cpp + lengthTokens.cpp, finished with stencil_cli_resolveCrop's
// integer rounding/clamping — the bot has no core/ access, so like mcp it ports the contract. Page
// metrics are A4 oriented to the image, mirroring the CLI's pipeline.namedPageForImage.
public static partial class CropSpecResolver
{
    private const double _cmPerInch = 2.54;
    private const double _a4ShortCm = 21.0;
    private const double _a4LongCm = 29.7;

    private enum LengthKind { Px, Cm, Percent, Delta }

    private readonly record struct LengthToken(LengthKind Kind, double Value, bool FromEnd);

    private sealed record ParsedSpec(string? X1, string? X2, string? Y1, string? Y2, string? Aspect, bool Valid);

    // Null when the spec is malformed or the rect rounds to empty. Edges default to the full image;
    // a single-axis spec derives the other from the page aspect; aspect=W:H centre-fits the rect.
    public static CropRect? Resolve(string spec, double imageW, double imageH, bool album)
    {
        ParsedSpec parsed = parse(spec);
        if (!parsed.Valid)
        {
            return null;
        }
        double pageW = imageW > imageH ? _a4LongCm : _a4ShortCm;
        double pageH = imageW > imageH ? _a4ShortCm : _a4LongCm;
        double pxPerCmX = imageW / pageW;
        double pxPerCmY = imageH / pageH;

        bool ok = true;
        double Edge(string? token, double current, double lengthPx, double pxPerCm)
        {
            if (token is null)
            {
                return current;
            }
            double? resolved = resolveAxisPx(token, lengthPx, pxPerCm, current);
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
            double aspect = cropAspect(pageW, pageH, album);   // width / height
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

        // Aspect fit exactly like the core: a centered shrink that never grows and keeps ≥ 1px.
        if (parsed.Aspect is string aspectToken)
        {
            double ratio = parseAspectRatio(aspectToken);   // width / height
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
            fitW = Math.Min(rectW, Math.Max(fitW, 1.0));
            fitH = Math.Min(rectH, Math.Max(fitH, 1.0));
            rectX += (rectW - fitW) / 2.0;
            rectY += (rectH - fitH) / 2.0;
            rectW = fitW;
            rectH = fitH;
        }

        // Final integer rounding, as stencil_cli_resolveCrop does it.
        int iw = lRound(imageW);
        int ih = lRound(imageH);
        int x = Math.Clamp(lRound(rectX), 0, Math.Max(0, iw));
        int y = Math.Clamp(lRound(rectY), 0, Math.Max(0, ih));
        int w = Math.Clamp(lRound(rectW), 0, iw - x);
        int h = Math.Clamp(lRound(rectH), 0, ih - y);
        if (w <= 0 || h <= 0)
        {
            return null;   // an empty crop is not useful
        }
        return new CropRect(x, y, w, h);
    }

    // Round half away from zero, like the C++ lround.
    private static int lRound(double value) => (int)Math.Round(value, MidpointRounding.AwayFromZero);

    private static double cropAspect(double pageW, double pageH, bool album)
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
