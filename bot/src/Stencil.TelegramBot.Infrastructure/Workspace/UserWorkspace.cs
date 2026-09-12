using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Infrastructure.Workspace;

// <DataDir>/<userId>, files named with a fresh GUID so writes never collide.
public sealed class UserWorkspace : IUserWorkspace
{
    private readonly BotOptions _options;

    public UserWorkspace(BotOptions options)
    {
        _options = options;
    }

    public string DirectoryFor(long userId)
    {
        string dir = Path.Combine(_options.DataDir, userId.ToString());
        Directory.CreateDirectory(dir);
        return dir;
    }

    public string NewFilePath(long userId, string extension)
    {
        string dir = DirectoryFor(userId);
        string name = Guid.NewGuid().ToString("N") + normalizeExtension(extension);
        return Path.Combine(dir, name);
    }

    public async Task<string> WriteAsync(long userId, byte[] data, string extension, CancellationToken ct = default)
    {
        string path = NewFilePath(userId, extension);
        await File.WriteAllBytesAsync(path, data, ct).ConfigureAwait(false);
        return path;
    }

    public void Clear(long userId)
    {
        string dir = Path.Combine(_options.DataDir, userId.ToString());
        try
        {
            if (Directory.Exists(dir))
            {
                Directory.Delete(dir, recursive: true);
            }
        }
        catch (DirectoryNotFoundException)
        {
        }
    }

    public IEnumerable<long> ActiveUserIds()
    {
        if (!Directory.Exists(_options.DataDir))
        {
            yield break;
        }
        foreach (string dir in Directory.EnumerateDirectories(_options.DataDir))
        {
            if (long.TryParse(Path.GetFileName(dir), out long userId))
            {
                yield return userId;
            }
        }
    }

    public int PruneStale(long userId, IReadOnlyCollection<string> keep, DateTime cutoffUtc)
    {
        string dir = Path.Combine(_options.DataDir, userId.ToString());
        if (!Directory.Exists(dir))
        {
            return 0;
        }
        HashSet<string> kept = new(keep.Select(normalizePath), StringComparer.Ordinal);
        int deleted = 0;
        // Recurse: scrape mode writes into a nested scrape-<guid>/ subdir that a top-level sweep
        // would never reap.
        foreach (string file in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories))
        {
            if (kept.Contains(normalizePath(file)))
            {
                continue; // still referenced by the session — never sweep it
            }
            if (File.GetLastWriteTimeUtc(file) >= cutoffUtc)
            {
                continue; // too recent — could be an in-flight render being sent right now
            }
            try
            {
                File.Delete(file);
                deleted++;
            }
            catch
            {
                // Retried next sweep.
            }
        }
        removeEmptyDescendants(dir);
        tryRemoveIfEmpty(dir);
        return deleted;
    }

    // Deepest first, so an emptied scrape-<guid>/ doesn't keep the user dir alive.
    private static void removeEmptyDescendants(string root)
    {
        try
        {
            foreach (string sub in Directory
                         .EnumerateDirectories(root, "*", SearchOption.AllDirectories)
                         .OrderByDescending(p => p.Length))
            {
                tryRemoveIfEmpty(sub);
            }
        }
        catch
        {
        }
    }

    private static string normalizePath(string path) => Path.GetFullPath(path);

    private static void tryRemoveIfEmpty(string dir)
    {
        try
        {
            if (Directory.Exists(dir) && !Directory.EnumerateFileSystemEntries(dir).Any())
            {
                Directory.Delete(dir);
            }
        }
        catch
        {
        }
    }

    private static string normalizeExtension(string extension)
    {
        if (string.IsNullOrWhiteSpace(extension))
        {
            return "";
        }
        string trimmed = extension.Trim();
        return trimmed.StartsWith('.') ? trimmed : "." + trimmed;
    }
}
