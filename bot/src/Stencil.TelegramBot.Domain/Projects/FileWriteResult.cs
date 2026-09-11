namespace Stencil.TelegramBot.Domain.Projects;

// protocol FileWriteResponse. The server is codec-free, so the caller passes width/height in.
public sealed record FileWriteResult(string Path, int W, int H);

// The three of the server's file kinds the bot touches (the full set, contract §9, is
// original | result | video | variant1..8 | chat).
public static class ProjectFileKind
{
    // Part of the project record: an upload bumps the version.
    public const string Original = "original";
    public const string Result = "result";

    // The §12 chat JSON (ext=json). Filestore-only: uploads and deletes never bump the version.
    public const string Chat = "chat";
}
