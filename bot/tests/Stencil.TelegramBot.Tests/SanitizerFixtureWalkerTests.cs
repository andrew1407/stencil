using System.Text.Json;
using System.Text.RegularExpressions;
using Stencil.TelegramBot.Infrastructure.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The shared sanitizer vectors through <see cref="HttpLlmClient.SanitizeProviderText"/>, one
/// test per vector. The bot slices UTF-16 code units exactly like the browser, so every vector
/// — the two <c>DIVERGENCE(...)</c> cases included — is asserted against the browser's literal
/// expect; those two also re-check the invariants (no URL, no token run, ≤ 200 units).
/// </summary>
public sealed class SanitizerFixtureWalkerTests
{
    private static readonly Regex _urlish = new(@"[a-z][a-z0-9+.-]*://\S+", RegexOptions.IgnoreCase);
    private static readonly Regex _bareTokenRun = new("[A-Za-z0-9_-]{24,}");

    private static string Corpus => Path.Combine(SharedFixtures.LlmFixtureDir("sanitizer"), "cases.json");

    public static TheoryData<string> Vectors() => SharedFixtures.TheoryNames(SharedFixtures.CaseNames(Corpus));

    [Fact]
    public void TheCorpusHasEveryVector() => Assert.Equal(19, SharedFixtures.Cases(Corpus).Count);

    [Theory]
    [MemberData(nameof(Vectors))]
    public void VectorMatches(string name)
    {
        using JsonDocument doc = SharedFixtures.Case(Corpus, name);
        JsonElement fx = doc.RootElement;
        JsonElement input = fx.GetProperty("input");
        // null input: SanitizeProviderText takes a non-null string; the null case is
        // JsonRead.ErrorDetail's "" — feed "" and expect "" per the schema.
        string text = input.ValueKind == JsonValueKind.Null ? "" : lenientString(input);

        string got = HttpLlmClient.SanitizeProviderText(text);

        Assert.Equal(lenientString(fx.GetProperty("expect")), got);
        if (!name.StartsWith("DIVERGENCE(", StringComparison.Ordinal))
        {
            return;
        }
        // Recomputed invariants, independent of the literal: bounded, no URL, and no bare
        // token-shaped run outside the redaction marker itself.
        Assert.True(got.Length <= HttpLlmClient.MAX_PROVIDER_DETAIL,
            $"output exceeds {HttpLlmClient.MAX_PROVIDER_DETAIL} UTF-16 units ({got.Length})");
        string unredacted = got.Replace("[redacted]", "");
        Assert.False(_urlish.IsMatch(unredacted), "a URL survived sanitization");
        Assert.False(_bareTokenRun.IsMatch(unredacted), "a token-shaped run survived sanitization");
    }

    /// <summary>
    /// GetString refuses a lone surrogate escape, which one DIVERGENCE expect ends in; fall
    /// back to unescaping the raw JSON text code-unit by code-unit.
    /// </summary>
    private static string lenientString(JsonElement element)
    {
        try
        {
            return element.GetString()!;
        }
        catch (InvalidOperationException)
        {
            string raw = element.GetRawText();
            return unescapeJson(raw[1..^1]);
        }
    }

    private static string unescapeJson(string s)
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
