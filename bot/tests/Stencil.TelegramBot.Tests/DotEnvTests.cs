using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The dev-time <c>.env</c> loader <see cref="DotEnv"/>: pure parsing of comments/blanks/quotes/
/// <c>export</c>, and the "real environment always wins" rule on <see cref="DotEnv.Load"/>.
/// </summary>
public sealed class DotEnvTests
{
    [Fact]
    public void ParseHandlesCommentsBlanksQuotesAndExport()
    {
        string text = string.Join(
            "\n",
            "# a comment",
            "",
            "  ",
            "PLAIN=value",
            "export EXPORTED=exp",
            "DQUOTED=\"quoted value\"",
            "SQUOTED='single'",
            "   SPACED   =   trimmed   ",
            "# trailing comment");
        IReadOnlyDictionary<string, string> parsed = DotEnv.Parse(text);
        Assert.Equal("value", parsed["PLAIN"]);
        Assert.Equal("exp", parsed["EXPORTED"]);
        Assert.Equal("quoted value", parsed["DQUOTED"]);
        Assert.Equal("single", parsed["SQUOTED"]);
        Assert.Equal("trimmed", parsed["SPACED"]);
        Assert.False(parsed.ContainsKey("# a comment"));
    }

    [Fact]
    public void LoadDoesNotOverrideAPresetVariable()
    {
        string key = "STENCIL_DOTENV_TEST_" + Guid.NewGuid().ToString("N");
        string file = Path.Combine(Path.GetTempPath(), "dotenv-" + Guid.NewGuid().ToString("N") + ".env");
        File.WriteAllText(file, key + "=from-file\n");
        try
        {
            Environment.SetEnvironmentVariable(key, "from-env");
            DotEnv.Load(file);
            Assert.Equal("from-env", Environment.GetEnvironmentVariable(key));
        }
        finally
        {
            Environment.SetEnvironmentVariable(key, null);
            File.Delete(file);
        }
    }

    [Fact]
    public void LoadSetsAnUnsetVariable()
    {
        string key = "STENCIL_DOTENV_TEST_" + Guid.NewGuid().ToString("N");
        string file = Path.Combine(Path.GetTempPath(), "dotenv-" + Guid.NewGuid().ToString("N") + ".env");
        File.WriteAllText(file, key + "=from-file\n");
        try
        {
            Environment.SetEnvironmentVariable(key, null);
            DotEnv.Load(file);
            Assert.Equal("from-file", Environment.GetEnvironmentVariable(key));
        }
        finally
        {
            Environment.SetEnvironmentVariable(key, null);
            File.Delete(file);
        }
    }

    [Fact]
    public void LoadOfMissingFileIsNoOp()
    {
        string missing = Path.Combine(Path.GetTempPath(), "dotenv-missing-" + Guid.NewGuid().ToString("N") + ".env");
        DotEnv.Load(missing);
    }
}
