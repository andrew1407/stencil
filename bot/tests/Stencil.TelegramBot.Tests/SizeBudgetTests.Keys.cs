using System.Text.Json;

namespace Stencil.TelegramBot.Tests;

/// <summary>The other half of the ratchet: every budget key must name something the measuring pass actually sees. A key left behind by a folder split is checked against nothing, so the file it used to hold is silently free to grow.</summary>
public sealed partial class SizeBudgetTests
{
    [Fact]
    public void Should_Name_A_File_For_Every_Budget_Key()
    {
        HashSet<string> paths = [.. _sources.Value.Select(f => f.Path)];
        HashSet<string> dirs = [.. paths.Select(dirOf)];
        List<string> dangling = [];
        foreach (string section in (string[])["files", "commentPct", "dirs"])
        {
            HashSet<string> known = section == "files" ? paths : dirs;
            foreach (JsonProperty key in Root.GetProperty(section).EnumerateObject())
            {
                if (!key.Name.StartsWith('_') && !known.Contains(key.Name))
                {
                    dangling.Add($"{section}: {key.Name}");
                }
            }
        }
        Assert.True(dangling.Count == 0,
            "budget key(s) naming no measured source — re-key them to where the code moved:\n  "
            + string.Join("\n  ", dangling));
    }
}
