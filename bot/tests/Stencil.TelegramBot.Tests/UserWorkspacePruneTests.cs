using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Workspace;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The workspace janitor's filesystem half: enumerating on-disk users, and pruning stale orphan
/// files while always keeping the session-referenced ones regardless of age.
/// </summary>
public sealed class UserWorkspacePruneTests : IDisposable
{
    private readonly string _root;
    private readonly IUserWorkspace _workspace;

    public UserWorkspacePruneTests()
    {
        _root = Path.Combine(Path.GetTempPath(), "stencil-bot-tests-" + Guid.NewGuid().ToString("N"));
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

    [Fact]
    public void ActiveUserIdsListsNumericDirectoriesOnly()
    {
        _workspace.DirectoryFor(7);
        _workspace.DirectoryFor(42);
        Directory.CreateDirectory(Path.Combine(_root, "not-a-user"));

        IReadOnlyList<long> ids = _workspace.ActiveUserIds();

        Assert.Equal(new[] { 7L, 42L }, ids.OrderBy(x => x));
    }

    [Fact]
    public void PruneStaleDeletesOldOrphansButKeepsReferencedAndRecent()
    {
        string referenced = _workspace.NewFilePath(7, ".png");
        string oldOrphan = _workspace.NewFilePath(7, ".png");
        string recentOrphan = _workspace.NewFilePath(7, ".png");
        File.WriteAllBytes(referenced, new byte[1]);
        File.WriteAllBytes(oldOrphan, new byte[1]);
        File.WriteAllBytes(recentOrphan, new byte[1]);

        DateTime now = DateTime.UtcNow;
        File.SetLastWriteTimeUtc(referenced, now - TimeSpan.FromHours(5)); // old, but referenced
        File.SetLastWriteTimeUtc(oldOrphan, now - TimeSpan.FromHours(5));  // old and orphaned → swept
        File.SetLastWriteTimeUtc(recentOrphan, now);                      // orphaned but too recent

        int deleted = _workspace.PruneStale(7, new[] { referenced }, cutoffUtc: now - TimeSpan.FromHours(1));

        Assert.Equal(1, deleted);
        Assert.True(File.Exists(referenced));
        Assert.False(File.Exists(oldOrphan));
        Assert.True(File.Exists(recentOrphan));
    }

    [Fact]
    public void PruneStaleRemovesADirectoryLeftEmpty()
    {
        string orphan = _workspace.NewFilePath(9, ".png");
        File.WriteAllBytes(orphan, new byte[1]);
        File.SetLastWriteTimeUtc(orphan, DateTime.UtcNow - TimeSpan.FromHours(5));

        _workspace.PruneStale(9, Array.Empty<string>(), cutoffUtc: DateTime.UtcNow - TimeSpan.FromHours(1));

        Assert.False(Directory.Exists(Path.Combine(_root, "9")));
    }

    [Fact]
    public void PruneStaleIsANoOpForAnUnknownUser()
    {
        Assert.Equal(0, _workspace.PruneStale(123, Array.Empty<string>(), DateTime.UtcNow));
    }

    [Fact]
    public void PruneStaleReapsNestedScrapeSubdirsAndRemovesTheEmptyUserDir()
    {
        // Scrape mode writes into a nested `scrape-<guid>/` subdir; the sweep must recurse into
        // it (a top-level-only sweep would leak it forever → disk exhaustion).
        string userDir = _workspace.DirectoryFor(11);
        string scrapeDir = Path.Combine(userDir, "scrape-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(scrapeDir);
        string nested = Path.Combine(scrapeDir, "photo.png");
        File.WriteAllBytes(nested, new byte[1]);
        File.SetLastWriteTimeUtc(nested, DateTime.UtcNow - TimeSpan.FromHours(5));

        int deleted = _workspace.PruneStale(11, Array.Empty<string>(), cutoffUtc: DateTime.UtcNow - TimeSpan.FromHours(1));

        Assert.Equal(1, deleted);
        Assert.False(File.Exists(nested));
        Assert.False(Directory.Exists(scrapeDir));         // emptied nested dir reaped
        Assert.False(Directory.Exists(Path.Combine(_root, "11"))); // now-empty user dir reaped
    }

    [Fact]
    public void PruneStaleKeepsANestedFileThatIsStillReferenced()
    {
        string userDir = _workspace.DirectoryFor(12);
        string scrapeDir = Path.Combine(userDir, "scrape-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(scrapeDir);
        string keptNested = Path.Combine(scrapeDir, "active.png");
        File.WriteAllBytes(keptNested, new byte[1]);
        File.SetLastWriteTimeUtc(keptNested, DateTime.UtcNow - TimeSpan.FromHours(5)); // old but referenced

        int deleted = _workspace.PruneStale(12, new[] { keptNested }, cutoffUtc: DateTime.UtcNow - TimeSpan.FromHours(1));

        Assert.Equal(0, deleted);
        Assert.True(File.Exists(keptNested)); // referenced nested file survives regardless of age
    }
}
