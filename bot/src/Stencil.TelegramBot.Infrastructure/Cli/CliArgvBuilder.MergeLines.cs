using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliArgvBuilder
{
    private const string _flagMergeLines = "--merge-lines";

    // The three line sets ride stdin, so no layout text reaches argv.
    public static IReadOnlyList<string> BuildMergeLinesArgv() => [_flagMergeLines, _stdin];

    // The --merge-lines input object: {"peer":[…],"local":[…],"seen":[…]}.
    public static string BuildMergeLinesInput(
        IReadOnlyList<LayoutLine> peer, IReadOnlyList<LayoutLine> local, IReadOnlyList<LayoutLine> seen) =>
        StencilJson.Serialize(new { peer, local, seen });
}
