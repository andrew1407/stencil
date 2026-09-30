using Stencil.TelegramBot.Application.Llm.Plan;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>The embedded registry the bot speaks from: byte-equal to the canonical file, and fingerprinted as the CLI reports its own so a CLI built from another registry is caught at startup.</summary>
public sealed class OpRegistryAssetTests
{
    [Fact]
    public void Should_Match_The_Canonical_File_Bytes_For_The_Embedded_Registry()
    {
        // The embed copies the shared registry at build time; catch drift against the repo's copy.
        byte[] canonical = File.ReadAllBytes(
            SharedFixtures.PathOf("common", "config", "llm", "opRegistry.json"));
        Assert.Equal(canonical, OpRegistryAsset.Bytes);
    }

    /// <summary>The skew check compares this with the CLI's registryFnv1a64: FNV-1a 64, 16 lowercase hex digits.</summary>
    [Theory]
    [InlineData("", "cbf29ce484222325")]
    [InlineData("a", "af63dc4c8601ec8c")]
    [InlineData("foobar", "85944171f73967e8")]
    public void Should_Fingerprint_As_The_Cli_Does(string text, string expected) =>
        Assert.Equal(expected, OpRegistryAsset.Fnv1a64Of(System.Text.Encoding.ASCII.GetBytes(text)));

    [Fact]
    public void Should_Match_Only_A_Check_Made_Against_The_Same_Registry()
    {
        Assert.True(OpRegistryAsset.Matches(new PlanCheck(OpRegistryAsset.Bytes.Length, OpRegistryAsset.Fnv1a64, "{}")));
        Assert.False(OpRegistryAsset.Matches(new PlanCheck(OpRegistryAsset.Bytes.Length, "0000000000000000", "{}")));
        Assert.False(OpRegistryAsset.Matches(new PlanCheck(OpRegistryAsset.Bytes.Length + 1, OpRegistryAsset.Fnv1a64, "{}")));
        Assert.Contains($"the bot one of {OpRegistryAsset.Bytes.Length} bytes", OpRegistryAsset.Describe(new PlanCheck(7, "ab", "{}")));
    }

    [Fact]
    public async Task Should_Report_No_Skew_Against_A_Cli_On_The_Same_Registry() =>
        Assert.Null(await OpPlanParser.RegistrySkewAsync(new MockStencilCli()));
}
