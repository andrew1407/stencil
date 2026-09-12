namespace Stencil.TelegramBot.Domain.Abstractions;

public interface IUserWorkspace
{
    string DirectoryFor(long userId);

    string NewFilePath(long userId, string extension);

    Task<string> WriteAsync(long userId, byte[] data, string extension, CancellationToken ct = default);

    void Clear(long userId);

    // Streamed: a busy bot's data dir is large.
    IEnumerable<long> ActiveUserIds();

    // Orphans not in keep, last written before cutoffUtc; a referenced file is kept regardless of
    // age.
    int PruneStale(long userId, IReadOnlyCollection<string> keep, DateTime cutoffUtc);
}
