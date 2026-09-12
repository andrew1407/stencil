namespace Stencil.TelegramBot.Infrastructure.Workspace;

public static class TempFiles
{
    // Ignores failures: a leftover temp file is harmless.
    public static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch
        {
        }
    }
}
