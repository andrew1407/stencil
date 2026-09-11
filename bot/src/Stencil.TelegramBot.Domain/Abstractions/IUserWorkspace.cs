namespace Stencil.TelegramBot.Domain.Abstractions;

// The on-disk scratch area for a user's working images — the bytes UserSession only references
// by path. Behind an interface so the editing services stay testable.
public interface IUserWorkspace
{
    // Created on demand.
    string DirectoryFor(long userId);

    string NewFilePath(long userId, string extension);

    Task<string> WriteAsync(long userId, byte[] data, string extension, CancellationToken ct = default);

    // Every file for a user; called on /reset.
    void Clear(long userId);

    // Streamed: a busy bot's data dir is large.
    IEnumerable<long> ActiveUserIds();

    // Deletes the orphans — not in keep, last written before cutoffUtc. A referenced file is kept
    // regardless of age. Returns the number deleted.
    int PruneStale(long userId, IReadOnlyCollection<string> keep, DateTime cutoffUtc);
}
