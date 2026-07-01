using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

/// <summary>
/// Discover the Stencil CLI binary the bot shells out to. A faithful port of
/// <c>mcp/src/locate.rs</c>.
/// </summary>
/// <remarks>
/// Resolution order (first hit wins): an explicit override / <c>STENCIL_CLI</c> env var (must
/// be an existing file), then the repo checkout — walk up from the CWD <i>and</i> the running
/// executable's directory for the nearest ancestor containing <c>cli/build.zig</c>, then
/// <c>cli/zig-out/bin/stencil</c> — then <c>stencil</c> on <c>PATH</c>. The bot never builds
/// the CLI; it reports a clear, actionable error when the binary is missing.
/// </remarks>
public static class StencilCliLocator
{
    /// <summary>The CLI binary's base name.</summary>
    private const string BinaryName = "stencil";

    /// <summary>The relative path of the built CLI inside a repo checkout.</summary>
    private const string RepoBinary = "cli/zig-out/bin/stencil";

    /// <summary>A marker that identifies the repo root unambiguously.</summary>
    private const string RepoMarker = "cli/build.zig";

    /// <summary>The actionable message thrown when no CLI binary can be found.</summary>
    public const string MissingMessage =
        "could not find the `stencil` CLI. Build it with `zig build` in `cli/`, " +
        "set the STENCIL_CLI env var to its path, or run the Docker image.";

    /// <summary>
    /// Resolve the CLI binary path. <paramref name="overridePath"/> (or the
    /// <c>STENCIL_CLI</c> env var when it is null/blank) takes precedence and must point at an
    /// existing file. Throws <see cref="StencilCliException"/> when nothing resolves.
    /// </summary>
    public static string FindCli(string? overridePath)
    {
        string? envOverride = string.IsNullOrWhiteSpace(overridePath)
            ? Environment.GetEnvironmentVariable("STENCIL_CLI")
            : overridePath;
        if (!string.IsNullOrWhiteSpace(envOverride))
        {
            if (File.Exists(envOverride))
            {
                return envOverride;
            }
            throw new StencilCliException($"STENCIL_CLI is set to '{envOverride}', which is not a file");
        }

        string? inRepo = FindInRepo();
        if (inRepo is not null)
        {
            return inRepo;
        }

        string? onPath = FindOnPath();
        if (onPath is not null)
        {
            return onPath;
        }

        throw new StencilCliException(MissingMessage);
    }

    /// <summary>
    /// Find the repo root (nearest ancestor containing <c>cli/build.zig</c>) above the CWD or
    /// the running executable's directory, or null when neither is inside a checkout.
    /// </summary>
    public static string? RepoRoot()
    {
        foreach (string start in StartDirs())
        {
            string? root = RepoRootFrom(start);
            if (root is not null)
            {
                return root;
            }
        }
        return null;
    }

    /// <summary>Candidate directories to start an upward search from (CWD then exe dir).</summary>
    private static IEnumerable<string> StartDirs()
    {
        List<string> starts = new();
        string cwd = Directory.GetCurrentDirectory();
        if (!string.IsNullOrEmpty(cwd))
        {
            starts.Add(cwd);
        }
        string baseDir = AppContext.BaseDirectory;
        if (!string.IsNullOrEmpty(baseDir))
        {
            starts.Add(baseDir);
        }
        return starts;
    }

    /// <summary>Look for <c>cli/zig-out/bin/stencil</c> under the nearest repo root above us.</summary>
    private static string? FindInRepo()
    {
        foreach (string start in StartDirs())
        {
            string? root = RepoRootFrom(start);
            if (root is null)
            {
                continue;
            }
            string candidate = Path.Combine(root, RepoBinary);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }
        return null;
    }

    /// <summary>Walk up from <paramref name="start"/> for an ancestor containing the marker.</summary>
    private static string? RepoRootFrom(string start)
    {
        DirectoryInfo? dir = new(start);
        while (dir is not null)
        {
            string marker = Path.Combine(dir.FullName, RepoMarker);
            if (File.Exists(marker))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }
        return null;
    }

    /// <summary>Scan <c>PATH</c> for an executable file named <c>stencil</c>.</summary>
    private static string? FindOnPath()
    {
        string? path = Environment.GetEnvironmentVariable("PATH");
        if (path is null)
        {
            return null;
        }
        foreach (string dir in path.Split(Path.PathSeparator))
        {
            if (string.IsNullOrEmpty(dir))
            {
                continue;
            }
            string candidate = Path.Combine(dir, BinaryName);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }
        return null;
    }
}
