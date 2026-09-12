using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

// A port of mcp/src/locate.rs. Order: an explicit override / STENCIL_CLI (must exist), then the
// nearest ancestor of the CWD or the executable holding cli/build.zig → cli/zig-out/bin/stencil,
// then PATH.
public static class StencilCliLocator
{
    private const string BinaryName = "stencil";

    private const string RepoBinary = "cli/zig-out/bin/stencil";

    private const string RepoSentinel = "cli/build.zig";

    public const string MissingMessage =
        "could not find the `stencil` CLI. Build it with `zig build` in `cli/`, " +
        "set the STENCIL_CLI env var to its path, or run the Docker image.";

    // The fix is the operator's, not the user's.
    public const string UnavailableMessage = "The image engine isn't available on this bot right now.";

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
            throw StencilCliException.Deployment(
                UnavailableMessage, $"STENCIL_CLI is set to '{envOverride}', which is not a file");
        }

        string? inRepo = findInRepo();
        if (inRepo is not null)
        {
            return inRepo;
        }

        string? onPath = findOnPath();
        if (onPath is not null)
        {
            return onPath;
        }

        throw StencilCliException.Deployment(UnavailableMessage, MissingMessage);
    }

    public static string? RepoRoot()
    {
        foreach (string start in startDirs())
        {
            string? root = repoRootFrom(start);
            if (root is not null)
            {
                return root;
            }
        }
        return null;
    }

    private static IEnumerable<string> startDirs()
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

    private static string? findInRepo()
    {
        foreach (string start in startDirs())
        {
            string? root = repoRootFrom(start);
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

    private static string? repoRootFrom(string start)
    {
        DirectoryInfo? dir = new(start);
        while (dir is not null)
        {
            string sentinel = Path.Combine(dir.FullName, RepoSentinel);
            if (File.Exists(sentinel))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }
        return null;
    }

    private static string? findOnPath()
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
