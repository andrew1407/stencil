namespace Stencil.TelegramBot.Domain.Editing;

// A crop given against the VIEW — the stored crop turned by the edit's quarter turns — composed
// into one px window over the unturned original, the frame the render's crop cuts before it
// rotates; the twin of the CLI console's applyCrop and the editors' crop over a crop.
public sealed record CropComposition(string Spec, int ViewWidth, int ViewHeight, CropRect Window);

public static partial class CropSpecResolver
{
    // Window is the new crop in view pixels. Null when the spec resolves to nothing in the view.
    public static CropComposition? Compose(
        string spec, string? storedSpec, bool storedAlbum, int quarters, double originalW, double originalH)
    {
        CropRect stored = (storedSpec is null ? null : Resolve(storedSpec, originalW, originalH, storedAlbum))
            ?? new CropRect(0, 0, lRound(originalW), lRound(originalH));
        int q = ((quarters % 4) + 4) % 4;
        (int viewW, int viewH) = q % 2 == 0 ? (stored.Width, stored.Height) : (stored.Height, stored.Width);
        if (Resolve(spec, viewW, viewH, album: false) is not CropRect window)
        {
            return null;
        }
        if (storedSpec is null && q == 0)
        {
            return new CropComposition(spec, viewW, viewH, window);   // the view IS the original
        }
        CropRect local = unturn(window, q, stored.Width, stored.Height);
        int x = stored.X + local.X;
        int y = stored.Y + local.Y;
        string composed = $"x1={x}px x2={x + local.Width}px y1={y}px y2={y + local.Height}px";
        return new CropComposition(composed, viewW, viewH, window);
    }

    // The view is the w x h stored window turned q clockwise quarters, (x, y) -> (h - y, x) each.
    private static CropRect unturn(CropRect r, int q, int w, int h) => q switch
    {
        1 => new CropRect(r.Y, h - r.X - r.Width, r.Height, r.Width),
        2 => new CropRect(w - r.X - r.Width, h - r.Y - r.Height, r.Width, r.Height),
        3 => new CropRect(w - r.Y - r.Height, r.X, r.Height, r.Width),
        _ => r,
    };
}
