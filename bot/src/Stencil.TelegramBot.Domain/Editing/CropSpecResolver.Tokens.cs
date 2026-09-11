using System.Globalization;
using System.Text;

namespace Stencil.TelegramBot.Domain.Editing;

// CropSpecResolver — the spec's text side: the atom tokenizer, the aspect ratio and the length
// tokens (px/cm/mm/in/%/delta). Port notes live in CropSpecResolver.cs.
public static partial class CropSpecResolver
{
    /// <summary>
    /// Tokenize into atoms split on whitespace/commas with <c>=</c> as its own atom, then
    /// consume <c>key = value</c> pairs; keys are case-insensitive x1/x2/y1/y2/aspect.
    /// </summary>
    private static ParsedSpec Parse(string spec)
    {
        List<string> atoms = new();
        StringBuilder current = new();
        void Flush()
        {
            if (current.Length > 0)
            {
                atoms.Add(current.ToString());
                current.Clear();
            }
        }
        foreach (char c in spec)
        {
            if (c is ' ' or '\t' or '\n' or '\r' or ',')
            {
                Flush();
            }
            else if (c == '=')
            {
                Flush();
                atoms.Add("=");
            }
            else
            {
                current.Append(c);
            }
        }
        Flush();

        string? x1 = null, x2 = null, y1 = null, y2 = null, aspect = null;
        bool valid = true;
        int i = 0;
        while (i < atoms.Count)
        {
            string key = atoms[i++];
            if (i >= atoms.Count || atoms[i] != "=")
            {
                valid = false;
                break;
            }
            i++;   // consume '='
            if (i >= atoms.Count || atoms[i] == "=")
            {
                valid = false;
                break;
            }
            string value = atoms[i++];
            switch (key.ToLowerInvariant())
            {
                case "x1": x1 = value; break;
                case "x2": x2 = value; break;
                case "y1": y1 = value; break;
                case "y2": y2 = value; break;
                case "aspect": aspect = value; break;
                default: valid = false; break;
            }
            if (!valid)
            {
                break;
            }
        }
        return new ParsedSpec(x1, x2, y1, y2, aspect, valid);
    }

    /// <summary>"W:H" with positive integers → W/H; 0.0 for anything else (zero, sign, junk).</summary>
    private static double ParseAspectRatio(string s)
    {
        int colon = s.IndexOf(':');
        if (colon <= 0 || colon + 1 >= s.Length)
        {
            return 0.0;
        }
        string[] parts = [s[..colon], s[(colon + 1)..]];
        double[] vals = [0.0, 0.0];
        for (int i = 0; i < 2; i++)
        {
            foreach (char c in parts[i])
            {
                if (c is < '0' or > '9')
                {
                    return 0.0;
                }
                vals[i] = vals[i] * 10.0 + (c - '0');
            }
            if (vals[i] <= 0.0)
            {
                return 0.0;
            }
        }
        return vals[0] / vals[1];
    }

    /// <summary>
    /// One edge token to pixels: a bare number is a DELTA from the current edge (sign kept);
    /// a unit token is absolute, measured from the far end when <c>-</c>-prefixed.
    /// </summary>
    private static double? ResolveAxisPx(string token, double lengthPx, double pxPerCm, double currentPx)
    {
        if (ParseLengthToken(token) is not LengthToken t)
        {
            return null;
        }
        if (t.Kind == LengthKind.Delta)
        {
            return currentPx + t.Value;
        }
        double px = t.Kind switch
        {
            LengthKind.Px => t.Value,
            LengthKind.Cm => t.Value * pxPerCm,
            _ => t.Value / 100.0 * lengthPx,   // percent
        };
        return t.FromEnd ? lengthPx - px : px;
    }

    /// <summary>The hand-rolled equivalent of <c>/^(-)?\s*(\d*\.?\d+)\s*(px|cm|mm|in|%)?$/</c>.</summary>
    private static LengthToken? ParseLengthToken(string token)
    {
        string s = token.Trim().ToLowerInvariant();
        if (s.Length == 0)
        {
            return null;
        }
        int i = 0;
        bool fromEnd = false;
        if (s[i] == '-')
        {
            fromEnd = true;
            i++;
        }
        while (i < s.Length && char.IsWhiteSpace(s[i]))
        {
            i++;
        }
        // Number: \d*\.?\d+ (at least one digit, at most one dot, no trailing dot).
        int numStart = i;
        int dots = 0, digitsAfterDot = 0, digits = 0;
        while (i < s.Length)
        {
            char c = s[i];
            if (c is >= '0' and <= '9')
            {
                digits++;
                if (dots > 0)
                {
                    digitsAfterDot++;
                }
                i++;
            }
            else if (c == '.')
            {
                if (dots > 0)
                {
                    break;
                }
                dots++;
                i++;
            }
            else
            {
                break;
            }
        }
        if (digits == 0 || (dots > 0 && digitsAfterDot == 0))
        {
            return null;
        }
        double value = double.Parse(s[numStart..i], CultureInfo.InvariantCulture);
        while (i < s.Length && char.IsWhiteSpace(s[i]))
        {
            i++;
        }
        return s[i..] switch
        {
            "%" => new LengthToken(LengthKind.Percent, value, fromEnd),
            "cm" => new LengthToken(LengthKind.Cm, value, fromEnd),
            "mm" => new LengthToken(LengthKind.Cm, value / 10.0, fromEnd),
            "in" => new LengthToken(LengthKind.Cm, value * CmPerInch, fromEnd),
            "px" => new LengthToken(LengthKind.Px, value, fromEnd),
            "" => new LengthToken(LengthKind.Delta, fromEnd ? -value : value, false),
            _ => null,   // unknown unit suffix
        };
    }
}
