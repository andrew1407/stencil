using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Project;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The shared <c>.stencil</c> vectors through <see cref="StencilProjectFile.Parse"/> at
/// verdict level, one test per vector — the schema's project shape and error text are the
/// browser's wording, so other surfaces match the case, not the text. Two ok-vectors the bot
/// rejects are pinned in <c>FixtureOverrides.json</c>; the rejection escapes as an
/// InvalidOperationException, not a null (see the last test).
/// </summary>
public sealed class StencilProjectFixtureWalkerTests
{
    private static readonly string[] _files = ["valid.json", "invalid.json"];

    private static string pathFor(string file) =>
        Path.Combine(SharedFixtures.ConfigFixtureDir("stencilProject"), file);

    public static TheoryData<string> Vectors() =>
        SharedFixtures.TheoryNames(_files.SelectMany(f => SharedFixtures.CaseNames(pathFor(f))));

    [Fact]
    public void Should_Have_Every_Vector_In_The_Corpus() =>
        Assert.Equal(21, _files.Sum(f => SharedFixtures.Cases(pathFor(f)).Count));

    [Theory]
    [MemberData(nameof(Vectors))]
    public void Should_Give_Each_Vector_Its_Verdict(string name)
    {
        string file = _files.First(f => SharedFixtures.CaseNames(pathFor(f)).Contains(name));
        using JsonDocument doc = SharedFixtures.Case(pathFor(file), name);
        JsonElement fx = doc.RootElement;
        string want = SharedFixtures.OverrideFor("stencilProject", name) is JsonElement ov
            ? ov.GetProperty("verdict").GetString()!
            : fx.GetProperty("expect").GetString()!;

        StencilProject? project;
        try
        {
            project = StencilProjectFile.Parse(Encoding.UTF8.GetBytes(fx.GetProperty("file").GetString()!));
        }
        catch (InvalidOperationException)
        {
            // The bot's non-null-returning failure mode (see the class remarks).
            project = null;
        }

        Assert.True(project is not null == (want == "ok"),
            $"expected {want}, got {(project is not null ? "a project" : "a rejection")}");
        if (project is not null)
        {
            Assert.True(project.ImageBytes.Length > 0, "an accepted project must carry image bytes");
        }
    }

    [Fact]
    public void Should_Pin_Rejection_Mechanisms_Exactly()
    {
        // version "1" (string): TryGetInt32 on a non-number THROWS — the reject is an
        // InvalidOperationException escaping Parse, not a null return.
        Assert.Throws<InvalidOperationException>(() => StencilProjectFile.Parse(Encoding.UTF8.GetBytes(
            "{\"format\":\"stencil-project\",\"version\":\"1\",\"image\":{\"dataUrl\":\"data:image/png;base64,AAAA\"}}")));
        // Untrimmed dataUrl: IndexOf("base64,") accepts leading whitespace — same verdict
        // as the browser's data-url-leading-whitespace-accepted-preserved vector.
        Assert.NotNull(StencilProjectFile.Parse(Encoding.UTF8.GetBytes(
            "{\"format\":\"stencil-project\",\"version\":1,\"image\":{\"dataUrl\":\"  data:image/png;base64,DDDD\"}}")));
    }
}
