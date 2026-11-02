using System.Globalization;

namespace Stencil.TelegramBot.Domain.Editing;

// A length's number, N or N/D with N, D each \d+(\.\d+)?|\.\d+ and D non-zero; the same grammar as
// the op registry's CROP_TOKEN and core's lengthTokens.cpp. TryParseCm adds a cm|mm|in suffix.
public static class LengthNumber
{
    public const double CmPerInch = 2.54;

    // The index just past the number starting at `start`, or -1 when none (or D is zero) is there.
    public static int Scan(string s, int start, out double value)
    {
        value = 0.0;
        int end = scanDecimal(s, start, out double numerator);
        if (end < 0)
        {
            return -1;
        }
        if (end >= s.Length || s[end] != '/')
        {
            value = numerator;
            return end;
        }
        int denomEnd = scanDecimal(s, end + 1, out double denominator);
        if (denomEnd < 0 || denominator == 0.0)
        {
            return -1;
        }
        value = numerator / denominator;
        return denomEnd;
    }

    // A bare number is cm; mm and in convert to cm. Sign and range are the caller's.
    public static bool TryParseCm(string text, out double cm)
    {
        cm = 0.0;
        string s = text.Trim().ToLowerInvariant();
        int end = Scan(s, 0, out double value);
        if (end < 0)
        {
            return false;
        }
        double? converted = s[end..] switch
        {
            "" or "cm" => value,
            "mm" => value / 10.0,
            "in" => value * CmPerInch,
            _ => null,
        };
        cm = converted ?? 0.0;
        return converted is not null;
    }

    // \d+(\.\d+)?|\.\d+: at least one digit, at most one dot, no trailing dot.
    private static int scanDecimal(string s, int start, out double value)
    {
        value = 0.0;
        int i = start;
        int dots = 0, digits = 0, digitsAfterDot = 0;
        while (i < s.Length)
        {
            char c = s[i];
            if (c is >= '0' and <= '9')
            {
                digits++;
                digitsAfterDot += dots;
                i++;
            }
            else if (c == '.' && dots == 0)
            {
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
            return -1;
        }
        value = double.Parse(s[start..i], CultureInfo.InvariantCulture);
        return i;
    }
}
