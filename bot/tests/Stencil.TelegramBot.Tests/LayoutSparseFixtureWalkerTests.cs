using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Walks the shared sparse-layout vectors
/// (<c>browser/js/config/fixtures/layout/sparse.json</c>, see <c>_schema.md</c>) through
/// <see cref="StencilLayoutParser"/>: each vector's sparse lines are wrapped as
/// <c>{"lines": …}</c> and the parsed lines must equal <c>expectFilled</c> — the sparse
/// input with the cross-surface per-line defaults filled in. The bot's typed
/// System.Text.Json binding is stricter than the reference sanitizer (no numeric-string
/// coercion, no junk skipping); those vectors are pinned as rejects in
/// <c>FixtureOverrides.json</c>. (payload.json is the browser's export-payload builder —
/// the bot has no equivalent and does not walk it. Since Phase 6 the bot's
/// <see cref="StencilLayout.Filter"/> reads BOTH top-level keys — the canonical
/// <c>imageFilter</c> the browser exports wins over the legacy <c>filter</c> — and
/// writes only <c>imageFilter</c>.)
/// </summary>
public sealed class LayoutSparseFixtureWalkerTests
{
    [Fact]
    public void EverySparseVectorFillsDefaultsOrIsPinnedAsReject()
    {
        List<string> failures = new();
        int walked = 0;
        using JsonDocument doc = SharedFixtures.Load(
            Path.Combine(SharedFixtures.ConfigFixtureDir("layout"), "sparse.json"));
        foreach (JsonElement fx in doc.RootElement.EnumerateArray())
        {
            walked++;
            string name = fx.GetProperty("name").GetString()!;
            byte[] bytes = Encoding.UTF8.GetBytes($"{{\"lines\":{fx.GetProperty("sparse").GetRawText()}}}");
            StencilLayout? layout = StencilLayoutParser.Parse(bytes);

            bool reject = SharedFixtures.OverrideFor("layoutSparse", name) is JsonElement ov
                && ov.GetProperty("verdict").GetString() == "reject";
            if (reject)
            {
                if (layout is not null)
                {
                    failures.Add($"{name}: pinned as reject but Parse returned a layout");
                }
                continue;
            }
            if (layout is null)
            {
                failures.Add($"{name}: Parse returned null");
                continue;
            }
            CompareLines(name, layout.Lines, fx.GetProperty("expectFilled"), failures);
        }
        Assert.Equal(10, walked);
        Assert.True(failures.Count == 0,
            $"{failures.Count} sparse-layout mismatches (walked {walked}):\n" + string.Join("\n", failures));
    }

    private static void CompareLines(
        string name, IReadOnlyList<LayoutLine> got, JsonElement expect, List<string> failures)
    {
        if (got.Count != expect.GetArrayLength())
        {
            failures.Add($"{name}: {got.Count} lines != {expect.GetArrayLength()}");
            return;
        }
        int i = 0;
        foreach (JsonElement want in expect.EnumerateArray())
        {
            LayoutLine line = got[i];
            void Check<T>(string field, T actual, T wanted)
            {
                if (!EqualityComparer<T>.Default.Equals(actual, wanted))
                {
                    failures.Add($"{name}: line {i} {field} {actual} != {wanted}");
                }
            }
            Check("color", line.Color, want.GetProperty("color").GetString()!);
            Check("thickness", line.Thickness, want.GetProperty("thickness").GetDouble());
            Check("pointSize", line.PointSize, want.GetProperty("pointSize").GetDouble());
            Check("style", line.Style, want.GetProperty("style").GetString()!);
            Check("locked", line.Locked, want.GetProperty("locked").GetBoolean());
            Check("fillColor", line.FillColor, want.GetProperty("fillColor").GetString()!);
            Check("pointColor", line.PointColor, want.GetProperty("pointColor").GetString()!);
            JsonElement points = want.GetProperty("points");
            if (line.Points.Count != points.GetArrayLength())
            {
                failures.Add($"{name}: line {i} has {line.Points.Count} points != {points.GetArrayLength()}");
            }
            else
            {
                int j = 0;
                foreach (JsonElement p in points.EnumerateArray())
                {
                    Check($"points[{j}].x", line.Points[j].X, p.GetProperty("x").GetDouble());
                    Check($"points[{j}].y", line.Points[j].Y, p.GetProperty("y").GetDouble());
                    j++;
                }
            }
            i++;
        }
    }
}
