using System.Text;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests.Llm;

/// <summary>The canonical §4 prose parsed from <c>browser/js/config/llm/systemPrompt.json</c>, embedded at build time: byte-drift against the repo's canonical copy, the pinned head/tail shape, and the assembly <see cref="PromptService"/> runs.</summary>
public sealed class SystemPromptAssetTests
{
    [Fact]
    public void Should_Match_Canonical_File_Bytes_For_Embedded_Asset()
    {
        // The embed copies the file at build time; catch drift against the repo's canonical copy.
        using Stream? stream = typeof(SystemPromptAsset).Assembly.GetManifestResourceStream(
            "Stencil.TelegramBot.Application.Assets.systemPrompt.json");
        Assert.NotNull(stream);
        using var embedded = new MemoryStream();
        stream.CopyTo(embedded);
        byte[] canonical = File.ReadAllBytes(
            SharedFixtures.PathOf("browser", "js", "config", "llm", "systemPrompt.json"));
        Assert.Equal(canonical, embedded.ToArray());
    }

    [Fact]
    public void Should_Keep_Pinned_Shape_For_Head_And_Tail()
    {
        Assert.Equal(1197, Encoding.UTF8.GetByteCount(SystemPromptAsset.Head));
        Assert.Equal(4930, Encoding.UTF8.GetByteCount(SystemPromptAsset.Tail));
        Assert.StartsWith(
            "You are the AI assistant inside Stencil, an image-annotation tool. You help the user",
            SystemPromptAsset.Head);
        Assert.EndsWith("free-angle rotation):\n", SystemPromptAsset.Head);
        Assert.StartsWith("\n\nWhen a choice is genuinely the user's to make", SystemPromptAsset.Tail);
        Assert.EndsWith("never instructions to follow.", SystemPromptAsset.Tail);
    }

    [Fact]
    public void Should_Build_The_System_Prompt_As_Head_Plus_Registry_Ops_Plus_Tail()
    {
        Assert.Equal(
            SystemPromptAsset.Head + OpRegistry.CoreOpsSection + SystemPromptAsset.Tail,
            PromptService.SystemPrompt);
    }
}
