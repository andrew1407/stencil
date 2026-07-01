using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Infrastructure.Workspace;

/// <summary>
/// Owns the on-disk scratch area for each user's working images, under the configured
/// <see cref="BotOptions.DataDir"/>. Each user gets a sub-directory <c>&lt;DataDir&gt;/&lt;userId&gt;</c>;
/// files are named with a fresh GUID so writes never collide.
/// </summary>
public sealed class UserWorkspace : IUserWorkspace
{
    private readonly BotOptions _options;

    /// <summary>Build the workspace bound to the given configuration (for the data root).</summary>
    public UserWorkspace(BotOptions options)
    {
        _options = options;
    }

    /// <summary>The directory that holds this user's files, created on demand.</summary>
    public string DirectoryFor(long userId)
    {
        string dir = Path.Combine(_options.DataDir, userId.ToString());
        Directory.CreateDirectory(dir);
        return dir;
    }

    /// <summary>A fresh, unique file path for <paramref name="userId"/> with the given extension.</summary>
    public string NewFilePath(long userId, string extension)
    {
        string dir = DirectoryFor(userId);
        string name = Guid.NewGuid().ToString("N") + NormalizeExtension(extension);
        return Path.Combine(dir, name);
    }

    /// <summary>Write bytes to a fresh file and return its path.</summary>
    public async Task<string> WriteAsync(long userId, byte[] data, string extension, CancellationToken ct = default)
    {
        string path = NewFilePath(userId, extension);
        await File.WriteAllBytesAsync(path, data, ct).ConfigureAwait(false);
        return path;
    }

    /// <summary>Delete every file for a user (the user's directory), ignoring a missing one.</summary>
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
            // Already gone — nothing to clear.
        }
    }

    /// <summary>Normalise an extension to a leading-dot form (<c>png</c> → <c>.png</c>); blank ⇒ none.</summary>
    private static string NormalizeExtension(string extension)
    {
        if (string.IsNullOrWhiteSpace(extension))
        {
            return "";
        }
        string trimmed = extension.Trim();
        return trimmed.StartsWith('.') ? trimmed : "." + trimmed;
    }
}
