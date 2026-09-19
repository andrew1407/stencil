using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Tests;

/// <summary>The shared sparse-layout vectors through <see cref="StencilLayoutParser"/>, one test per vector. The bot's typed binding is stricter than the reference sanitizer (no numeric-string coercion, no junk skipping), so those vectors are pinned as rejects in <c>FixtureOverrides.json</c>.</summary>
public sealed class LayoutSparseFixtureWalkerTests
{
    private static string Corpus => Path.Combine(SharedFixtures.ConfigFixtureDir("layout"), "sparse.json");

    public static TheoryData<string> Vectors() => SharedFixtures.TheoryNames(SharedFixtures.CaseNames(Corpus));

    [Fact]
    public void Should_Have_Every_Vector_In_The_Corpus() => Assert.Equal(10, SharedFixtures.Cases(Corpus).Count);

    [Theory]
    [MemberData(nameof(Vectors))]
    public void Should_Fill_Defaults_Or_Pin_As_Reject_For_Each_Vector(string name)
    {
        using JsonDocument doc = SharedFixtures.Case(Corpus, name);
        JsonElement fx = doc.RootElement;
        byte[] bytes = Encoding.UTF8.GetBytes($"{{\"lines\":{fx.GetProperty("sparse").GetRawText()}}}");
        StencilLayout? layout = StencilLayoutParser.Parse(bytes);

        bool reject = SharedFixtures.OverrideFor("layoutSparse", name) is JsonElement ov
            && ov.GetProperty("verdict").GetString() == "reject";
        if (reject)
        {
            Assert.Null(layout); // pinned as reject
            return;
        }
        Assert.NotNull(layout);

        List<string> failures = new();
        compareLines(name, layout!.Lines, fx.GetProperty("expectFilled"), failures);
        Assert.True(failures.Count == 0, string.Join("\n", failures));
    }

    private static void compareLines(
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
