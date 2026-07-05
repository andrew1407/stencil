using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Workspace;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// SECURITY regressions for <see cref="UserWorkspace"/> path derivation: a hostile
/// Telegram-supplied <c>document.FileName</c> (e.g. <c>../../evil.png</c>) must never escape
/// the per-user workspace directory. The bot only ever derives a *file extension* from an
/// uploaded name (via <c>Path.GetExtension</c>, as <c>UpdateRouter.ExtensionOf</c> does) and
/// gives the file a fresh GUID name, so the attacker never controls a path component.
/// </summary>
public sealed class UserWorkspaceSecurityTests : IDisposable
{
    private const long UserId = 7;
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
    public void UploadFileNameCannotEscapeTheUserDirectory(string hostileFileName)
    {
        // Reproduce exactly what the router does with an uploaded document's name: take only
        // its extension. The name itself is discarded; a GUID becomes the real filename.
        string extension = Path.GetExtension(hostileFileName);

        string stored = _workspace.NewFilePath(UserId, extension);

        string userDir = Path.GetFullPath(_workspace.DirectoryFor(UserId));
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
    public void ExtensionDerivationNeverYieldsAPathSeparator(string hostileFileName)
    {
        // The linchpin of the safety argument: Path.GetExtension (what the router uses) can
        // never return a value containing a directory separator or a `..` segment — it is the
        // suffix after the last dot of the *last* path component. So the extension that
        // reaches NewFilePath is always separator-free, and a GUID basename + a separator-free
        // suffix cannot climb out of the user directory.
        string extension = Path.GetExtension(hostileFileName);

        Assert.DoesNotContain('/', extension);
        Assert.DoesNotContain('\\', extension);
        Assert.DoesNotContain("..", extension);
    }
}
