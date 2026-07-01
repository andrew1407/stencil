namespace Stencil.TelegramBot.Domain.Abstractions;

/// <summary>
/// Owns the on-disk scratch area for a user's working images (the bytes the small JSON
/// <see cref="Sessions.UserSession"/> only references by path). Implemented over a configured
/// data directory; kept behind an interface so the editing services stay testable.
/// </summary>
public interface IUserWorkspace
{
    /// <summary>The directory that holds this user's files (created on demand).</summary>
    string DirectoryFor(long userId);

    /// <summary>A fresh, unique file path for <paramref name="userId"/> with the given extension.</summary>
    string NewFilePath(long userId, string extension);

    /// <summary>Write bytes to a fresh file and return its path.</summary>
    Task<string> WriteAsync(long userId, byte[] data, string extension, CancellationToken ct = default);

    /// <summary>Delete every file for a user (called on /reset).</summary>
    void Clear(long userId);
}
