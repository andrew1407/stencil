namespace Stencil.TelegramBot.Domain.Projects;

// protocol FileWriteResponse. The server is codec-free, so the caller passes width/height in.
public sealed record FileWriteResult(string Path, int W, int H);

// The three of the server's file kinds (§9) the bot touches.
public static class ProjectFileKind
{
    // Part of the project record: an upload bumps the version.
    public const string ORIGINAL = "original";
    public const string RESULT = "result";

    // The §12 chat JSON (ext=json). Filestore-only: uploads and deletes never bump the version.
    public const string CHAT = "chat";
}
