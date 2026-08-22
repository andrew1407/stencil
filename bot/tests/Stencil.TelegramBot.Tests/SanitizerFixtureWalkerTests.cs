using System.Text.Json;
using System.Text.RegularExpressions;
using Stencil.TelegramBot.Infrastructure.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Walks the shared provider-error sanitizer vectors
/// (<c>browser/js/config/llm/fixtures/sanitizer/cases.json</c>, see <c>_schema.md</c>)
/// through <see cref="HttpLlmClient.SanitizeProviderText"/>. The bot slices UTF-16 code
/// units exactly like the browser (`t[..199]` can split a surrogate pair the same way
/// String.prototype.slice does), so every vector — the two <c>DIVERGENCE(...)</c> cases
/// included — is asserted against the browser's literal <c>expect</c>; the DIVERGENCE cases
/// additionally re-check the safety invariants (no URL, no token run, ≤ 200 units).
/// </summary>
public sealed class SanitizerFixtureWalkerTests
{
    private static readonly Regex Urlish = new(@"[a-z][a-z0-9+.-]*://\S+", RegexOptions.IgnoreCase);
    private static readonly Regex BareTokenRun = new("[A-Za-z0-9_-]{24,}");

    [Fact]
    public void EverySanitizerVectorMatches()
    {
        List<string> failures = new();
        int walked = 0;
        using JsonDocument doc = SharedFixtures.Load(
            Path.Combine(SharedFixtures.LlmFixtureDir("sanitizer"), "cases.json"));
        foreach (JsonElement fx in doc.RootElement.EnumerateArray())
        {
            walked++;
            string name = fx.GetProperty("name").GetString()!;
            JsonElement input = fx.GetProperty("input");
            // null input: SanitizeProviderText takes a non-null string; the null case is
            // JsonRead.ErrorDetail's "" — feed "" and expect "" per the schema.
            string text = input.ValueKind == JsonValueKind.Null ? "" : LenientString(input);
            string expect = LenientString(fx.GetProperty("expect"));

            string got = HttpLlmClient.SanitizeProviderText(text);
            if (got != expect)
            {
                failures.Add($"{name}: \"{got}\" != \"{expect}\"");
            }
            if (name.StartsWith("DIVERGENCE(", StringComparison.Ordinal))
            {
                // Recomputed invariants, independent of the literal: bounded, no URL, and no
                // bare token-shaped run outside the redaction marker itself.
                if (got.Length > HttpLlmClient.MaxProviderDetail)
                {
                    failures.Add($"{name}: output exceeds {HttpLlmClient.MaxProviderDetail} UTF-16 units ({got.Length})");
                }
                string unredacted = got.Replace("[redacted]", "");
                if (Urlish.IsMatch(unredacted))
                {
                    failures.Add($"{name}: a URL survived sanitization");
                }
                if (BareTokenRun.IsMatch(unredacted))
                {
                    failures.Add($"{name}: a token-shaped run survived sanitization");
                }
            }
        }
        Assert.Equal(19, walked);
        Assert.True(failures.Count == 0,
            $"{failures.Count} sanitizer mismatches (walked {walked}):\n" + string.Join("\n", failures));
    }

    /// <summary>
    /// <see cref="JsonElement.GetString"/> refuses a lone surrogate escape — which one
    /// DIVERGENCE vector's expect deliberately ends in (the browser's 199-code-unit cut
    /// splits an emoji). Fall back to unescaping the raw JSON text code-unit by code-unit.
    /// </summary>
    private static string LenientString(JsonElement element)
    {
        try
        {
            return element.GetString()!;
        }
        catch (InvalidOperationException)
        {
            string raw = element.GetRawText();
            return UnescapeJson(raw[1..^1]);
        }
    }

    private static string UnescapeJson(string s)
    {
        System.Text.StringBuilder sb = new(s.Length);
        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];
            if (c != '\\')
            {
                sb.Append(c);
                continue;
            }
            char e = s[++i];
            switch (e)
            {
                case 'u':
                    sb.Append((char)Convert.ToInt32(s.Substring(i + 1, 4), 16));
                    i += 4;
                    break;
                case 'n': sb.Append('\n'); break;
                case 't': sb.Append('\t'); break;
                case 'r': sb.Append('\r'); break;
                case 'b': sb.Append('\b'); break;
                case 'f': sb.Append('\f'); break;
                default: sb.Append(e); break; // \" \\ \/
            }
        }
        return sb.ToString();
    }
}
