using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Workspace;

namespace Stencil.TelegramBot.Tests;

/// <summary>SECURITY regressions for path derivation: a hostile Telegram-supplied <c>document.FileName</c> must never escape the per-user workspace. The bot derives only a file EXTENSION from an uploaded name (<c>Path.GetExtension</c>) and gives the file a fresh GUID name, so no path component is attacker-controlled.</summary>
public sealed class UserWorkspaceSecurityTests : IDisposable
{
    private const long _userId = 7;
    private readonly string _root;
    private readonly IUserWorkspace _workspace;

    public UserWorkspaceSecurityTests()
    {
        _root = Path.Combine(Path.GetTempPath(), "stencil-bot-sec-" + Guid.NewGuid().ToString("N"));
        _workspace = new UserWorkspace(new BotOptions { DataDir = _root });
    }

    public void Dispose()
    {
        try
        {
            Directory.Delete(_root, recursive: true);
        }
        catch
        {
            // Best effort — temp dir.
        }
    }

    [Theory]
    [InlineData("../../evil.png")]
    [InlineData("../../../../../../etc/passwd.png")]
    [InlineData("..\\..\\evil.png")]
    [InlineData("/etc/cron.d/evil.png")]
    [InlineData("subdir/evil.png")]
    [InlineData("evil.png")]
    [InlineData("no-extension")]
    [InlineData("")]
    [InlineData("weird.name.with.dots.jpg")]
    public void Should_Not_Let_An_Upload_File_Name_Escape_The_User_Directory(string hostileFileName)
    {
        // Reproduce exactly what the router does with an uploaded document's name: take only
        // its extension. The name itself is discarded; a GUID becomes the real filename.
        string extension = Path.GetExtension(hostileFileName);

        string stored = _workspace.NewFilePath(_userId, extension);

        string userDir = Path.GetFullPath(_workspace.DirectoryFor(_userId));
        string storedFull = Path.GetFullPath(stored);

        // The stored file sits directly inside the user's own directory — no traversal.
        Assert.Equal(userDir, Path.GetDirectoryName(storedFull));
        Assert.StartsWith(userDir + Path.DirectorySeparatorChar, storedFull);
        Assert.DoesNotContain("..", Path.GetFileName(storedFull));
    }

    [Theory]
    [InlineData("../../evil.png")]
    [InlineData("..\\..\\evil.png")]
    [InlineData("/etc/cron.d/evil.png")]
    [InlineData("subdir/evil.png")]
    [InlineData(".png/../../escape")]
    public void Should_Never_Yield_A_Path_Separator_From_Extension_Derivation(string hostileFileName)
    {
        // Path.GetExtension (what the router uses) returns the suffix after the last dot of the LAST component,
        // so it can hold no separator and no `..`; a GUID basename plus that cannot climb out.
        string extension = Path.GetExtension(hostileFileName);

        Assert.DoesNotContain('/', extension);
        Assert.DoesNotContain('\\', extension);
        Assert.DoesNotContain("..", extension);
    }
}
