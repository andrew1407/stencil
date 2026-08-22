using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Project;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Walks the shared <c>.stencil</c> project-file vectors
/// (<c>browser/js/config/fixtures/stencilProject/</c>, see <c>_schema.md</c>) through
/// <see cref="StencilProjectFile.Parse"/> at VERDICT level: <c>ok</c> = a project comes
/// back, <c>error</c> = the parse fails. The schema's <c>project</c> shape and
/// <c>errorIncludes</c> text are the browser's normalization/wording — other surfaces match
/// on the case, not the text. Two ok-vectors the bot rejects are pinned in
/// <c>FixtureOverrides.json</c>; note the rejection mechanism: non-number JSON where the
/// bot calls <c>TryGetInt32</c>/<c>GetString</c> throws InvalidOperationException (outside
/// Parse's JsonException/FormatException filter) rather than returning null.
/// </summary>
public sealed class StencilProjectFixtureWalkerTests
{
    [Fact]
    public void EveryProjectVectorGetsItsVerdict()
    {
        List<string> failures = new();
        int walked = 0;
        foreach (string file in new[] { "valid.json", "invalid.json" })
        {
            using JsonDocument doc = SharedFixtures.Load(
                Path.Combine(SharedFixtures.ConfigFixtureDir("stencilProject"), file));
            foreach (JsonElement fx in doc.RootElement.EnumerateArray())
            {
                walked++;
                string name = fx.GetProperty("name").GetString()!;
                string want = SharedFixtures.OverrideFor("stencilProject", name) is JsonElement ov
                    ? ov.GetProperty("verdict").GetString()!
                    : fx.GetProperty("expect").GetString()!;

                StencilProject? project;
                try
                {
                    project = StencilProjectFile.Parse(
                        Encoding.UTF8.GetBytes(fx.GetProperty("file").GetString()!));
                }
                catch (InvalidOperationException)
                {
                    // The bot's non-null-returning failure mode (see the class remarks).
                    project = null;
                }
                bool ok = project is not null;
                if (ok != (want == "ok"))
                {
                    failures.Add($"{file}/{name}: expected {want}, got {(ok ? "a project" : "a rejection")}");
                }
                if (ok && project!.ImageBytes.Length == 0)
                {
                    failures.Add($"{file}/{name}: an accepted project must carry image bytes");
                }
            }
        }
        Assert.Equal(21, walked);
        Assert.True(failures.Count == 0,
            $"{failures.Count} project-file mismatches (walked {walked}):\n" + string.Join("\n", failures));
    }

    [Fact]
    public void RejectionMechanismsArePinnedExactly()
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
