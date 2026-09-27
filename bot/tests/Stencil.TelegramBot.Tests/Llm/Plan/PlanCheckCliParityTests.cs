using System.Collections.Concurrent;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Application.Llm.Plan;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Cli;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>The CLI-gated half of the op-plan proof, run only when <c>BOT_TEST_CLI</c> names a built <c>cli/zig-out/bin/stencil</c>: the live <c>--plan-check</c> judges every golden case as <c>normalized.json</c> records it for the bot, embeds the bot's own registry, and agrees with every recording the offline double replays.</summary>
public sealed class PlanCheckCliParityTests
{
    [Fact]
    public async Task Should_Judge_Every_Golden_Case_As_The_Golden_Records()
    {
        if (PlanCheckRecordings.LiveCli is not string cli)
        {
            return;
        }
        Dictionary<string, string> texts = new(StringComparer.Ordinal);
        foreach (OpPlanFixture fx in OpPlanCorpus.All)
        {
            texts.TryAdd(fx.Name, fx.InputText);
        }
        foreach (OpPlanOracleCase oracle in OpPlanCorpus.Oracle)
        {
            texts.TryAdd(oracle.Name, oracle.InputText);
        }
        Assert.True(OpPlanCorpus.Golden.Count >= 400, $"the bot golden collapsed to {OpPlanCorpus.Golden.Count}");
        IReadOnlyList<string> drift = await driftAsync(cli, OpPlanCorpus.Golden.Select(g => (g.Key, texts[g.Key], g.Value)));
        Assert.True(drift.Count == 0, "the live CLI and normalized.json disagree on:\n  " + string.Join("\n  ", drift));
    }

    [Fact]
    public async Task Should_Agree_With_Every_Recording_The_Offline_Double_Replays()
    {
        if (PlanCheckRecordings.LiveCli is not string cli)
        {
            return;
        }
        IReadOnlyList<string> drift = await driftAsync(cli, PlanCheckRecordings.All.Select(r => (r.Key, r.Key, r.Value)));
        Assert.True(drift.Count == 0,
            "planChecks.json is stale — rerun with BOT_UPDATE_GOLDENS=1 after deleting these:\n  " + string.Join("\n  ", drift));
    }

    [Fact]
    public async Task Should_Run_A_Cli_Built_From_The_Bots_Own_Registry()
    {
        if (PlanCheckRecordings.LiveCli is not string cli)
        {
            return;
        }
        Assert.Null(await OpPlanParser.RegistrySkewAsync(PlanCheckRecordings.Live(cli)));
    }

    private static async Task<IReadOnlyList<string>> driftAsync(string cli, IEnumerable<(string Name, string Text, string Want)> cases)
    {
        ProcessStencilCli live = PlanCheckRecordings.Live(cli);
        ConcurrentBag<string> drift = new();
        await Parallel.ForEachAsync(cases, new ParallelOptions { MaxDegreeOfParallelism = 4 }, async (c, ct) =>
        {
            PlanCheck check = await live.PlanCheckAsync(c.Text, ct);
            if (!JsonNode.DeepEquals(JsonNode.Parse(check.Result), JsonNode.Parse(c.Want)))
            {
                drift.Add(c.Name.Length <= 80 ? c.Name : c.Name[..80] + "…");
            }
        });
        return [.. drift.Order(StringComparer.Ordinal)];
    }
}
