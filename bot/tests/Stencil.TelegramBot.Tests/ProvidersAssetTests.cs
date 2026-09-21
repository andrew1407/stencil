using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests;

/// <summary>The canonical provider constants parsed from <c>browser/js/config/llm/providers.json</c>, embedded at build time: byte-drift against the repo's canonical copy, plus the pinned parsed values.</summary>
public sealed class ProvidersAssetTests
{
    [Fact]
    public void Should_Match_Canonical_File_Bytes_For_Embedded_Asset()
    {
        // The embed copies the file at build time; catch drift against the repo's canonical copy.
        using Stream? stream = typeof(ProvidersAsset).Assembly.GetManifestResourceStream(
            "Stencil.TelegramBot.Domain.Assets.providers.json");
        Assert.NotNull(stream);
        using var embedded = new MemoryStream();
        stream.CopyTo(embedded);
        byte[] canonical = File.ReadAllBytes(
            SharedFixtures.PathOf("browser", "js", "config", "llm", "providers.json"));
        Assert.Equal(canonical, embedded.ToArray());
    }

    [Fact]
    public void Should_Keep_Pinned_Values_For_Parsed_Constants()
    {
        Assert.Equal(120, ProvidersAsset.ChatTimeoutSeconds);
        Assert.Equal(TimeSpan.FromSeconds(120), HttpLlmClient.DefaultTimeout);
        Assert.Equal("http://localhost:11434", ProvidersAsset.OllamaBaseUrl);
        Assert.Equal("http://localhost:1234/v1", ProvidersAsset.OpenAiCompatBaseUrl);
    }
}
