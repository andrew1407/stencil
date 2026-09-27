using System.Text.Json;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests.Cli;

/// <summary>The <c>--plan-check</c> envelope (cli/CONTRACT.md §7): one JSON document on stdout, its <c>result</c> kept raw for the typed mapper and its registry fingerprint kept for the skew check.</summary>
public sealed class CliOutcomePlanCheckTests
{
    private const string _envelope =
        """{"version":1,"surface":"bot","registryBytes":80237,"registryFnv1a64":"b06326deffbdf599","result":{"status":"valid","reply":"ok","actions":[{"op":"rotate","dir":"left","times":1}],"variants":[],"ask":null,"warnings":[],"error":null}}""";

    [Fact]
    public void Should_Keep_The_Result_And_The_Registry_Fingerprint()
    {
        PlanCheck check = CliOutcomeParser.ParsePlanCheck(_envelope + "\n");

        Assert.Equal(80237, check.RegistryBytes);
        Assert.Equal("b06326deffbdf599", check.RegistryFnv1a64);
        using JsonDocument result = JsonDocument.Parse(check.Result);
        Assert.Equal("valid", result.RootElement.GetProperty("status").GetString());
        Assert.Equal(1, result.RootElement.GetProperty("actions")[0].GetProperty("times").GetInt32());
    }

    [Theory]
    [InlineData("")]
    [InlineData("not json")]
    [InlineData("[1,2]")]
    [InlineData("""{"version":1,"surface":"bot"}""")]
    [InlineData("""{"version":1,"result":"valid"}""")]
    public void Should_Refuse_Anything_That_Is_Not_A_Plan_Check(string stdout) =>
        Assert.Throws<StencilCliException>(() => CliOutcomeParser.ParsePlanCheck(stdout));

    /// <summary>A bumped envelope means this consumer must change first, so it is never half-read.</summary>
    [Fact]
    public void Should_Refuse_Another_Envelope_Version() =>
        Assert.Contains("version 2", Assert.Throws<StencilCliException>(
            () => CliOutcomeParser.ParsePlanCheck(_envelope.Replace("\"version\":1", "\"version\":2"))).Message);
}
