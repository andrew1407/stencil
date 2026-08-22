namespace Stencil.TelegramBot.Infrastructure.Workspace;

/// <summary>Best-effort cleanup of throwaway files (probe outputs, downloads, temp renders).</summary>
public static class TempFiles
{
    /// <summary>Delete <paramref name="path"/>, ignoring failures — a leftover temp file is harmless.</summary>
    public static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch
        {
            // Best effort — ignore.
        }
    }
}
