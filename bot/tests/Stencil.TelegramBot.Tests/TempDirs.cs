namespace Stencil.TelegramBot.Tests;

/// <summary>The suites' scratch directories: a unique path under the temp root per test, removed
/// best-effort on dispose (a test that never wrote one leaves nothing to remove).</summary>
public static class TempDirs
{
    public static string New(string tag) => Path.Combine(Path.GetTempPath(), $"stencil-{tag}-{Guid.NewGuid():N}");

    public static void Delete(string dir)
    {
        try
        {
            Directory.Delete(dir, recursive: true);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }
}
