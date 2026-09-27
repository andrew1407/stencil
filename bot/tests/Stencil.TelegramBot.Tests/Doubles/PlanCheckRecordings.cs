using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Application.Llm.Plan;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Cli;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>The offline stand-in for <c>stencil --plan-check - --plan-surface bot</c>: core's verdict per reply, recorded from the real CLI into <c>planChecks.json</c>. A reply with no recording fails its test; <c>BOT_UPDATE_GOLDENS=1</c> with <see cref="LiveCli"/> set records it, and <c>PlanCheckCliParityTests</c> re-judges every recording against that CLI.</summary>
internal static class PlanCheckRecordings
{
    private static readonly string _path = SharedFixtures.PathOf(
        "bot", "tests", "Stencil.TelegramBot.Tests", "Doubles", "planChecks.json");

    private static readonly object _gate = new();
    private static readonly Lazy<SortedDictionary<string, string>> _recorded = new(load);

    /// <summary>The CLI the gated tests and the recorder run: <c>BOT_TEST_CLI</c>, else nothing — so a plain run stays offline.</summary>
    public static string? LiveCli =>
        Environment.GetEnvironmentVariable("BOT_TEST_CLI") is { Length: > 0 } path && File.Exists(path) ? path : null;

    public static ProcessStencilCli Live(string path) => new(new BotOptions { CliPath = path });

    public static IReadOnlyDictionary<string, string> All
    {
        get { lock (_gate) { return new Dictionary<string, string>(_recorded.Value, StringComparer.Ordinal); } }
    }

    public static async Task<PlanCheck> CheckAsync(string reply, CancellationToken ct)
    {
        lock (_gate)
        {
            if (_recorded.Value.TryGetValue(reply, out string? result))
            {
                return new PlanCheck(OpRegistryAsset.Bytes.Length, OpRegistryAsset.Fnv1a64, result);
            }
        }
        if (Environment.GetEnvironmentVariable("BOT_UPDATE_GOLDENS") != "1" || LiveCli is not string cli)
        {
            throw new InvalidOperationException(
                "no recorded plan check for this reply — rerun with BOT_UPDATE_GOLDENS=1 and BOT_TEST_CLI "
                + $"pointing at a built cli/zig-out/bin/stencil:\n{reply}");
        }
        PlanCheck live = await Live(cli).PlanCheckAsync(reply, ct).ConfigureAwait(false);
        lock (_gate)
        {
            _recorded.Value[reply] = live.Result;
            save();
        }
        return live;
    }

    /// <summary>Core's check of one <c>--script-plan</c> chunk: its plan object, reply empty, judged as a reply.</summary>
    public static string ScriptChunk(string actionsJson) =>
        CheckAsync($$"""{"reply":"","actions":{{actionsJson}}}""", CancellationToken.None).GetAwaiter().GetResult().Result;

    private static SortedDictionary<string, string> load()
    {
        SortedDictionary<string, string> recorded = new(StringComparer.Ordinal);
        if (!File.Exists(_path))
        {
            return recorded;
        }
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(_path));
        foreach (JsonProperty entry in doc.RootElement.GetProperty("checks").EnumerateObject())
        {
            recorded[entry.Name] = entry.Value.GetRawText();
        }
        return recorded;
    }

    // One reply per line, its verdict compact, so a re-record diffs by case.
    private static void save()
    {
        JsonSerializerOptions relaxed = new() { Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };
        StringBuilder text = new("{\n  \"$comment\": \"core's --plan-check result per reply the tests send, recorded from the real CLI; see PlanCheckRecordings\",\n  \"checks\": {\n");
        text.AppendJoin(",\n", _recorded.Value.Select(entry =>
            $"    {JsonSerializer.Serialize(entry.Key, relaxed)}: {JsonNode.Parse(entry.Value)!.ToJsonString(relaxed)}"));
        File.WriteAllText(_path, text.Append("\n  }\n}\n").ToString());
    }
}
