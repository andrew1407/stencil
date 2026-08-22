namespace Stencil.TelegramBot.Domain.Projects;

/// <summary>
/// Returned by <c>POST /projects/{id}/files/{kind}</c> (protocol <c>FileWriteResponse</c>):
/// the stored path and the dimensions the server recorded (it is codec-free, so the caller
/// passes width/height in).
/// </summary>
public sealed record FileWriteResult(string Path, int W, int H);

/// <summary>
/// The server's per-project file kinds the bot uses. The full set (contract §9) is
/// <c>original | result | video | variant1..8 | chat</c>; the bot only touches the three below.
/// <see cref="Original"/>/<see cref="Result"/> are part of the project record (their uploads
/// bump the version); <see cref="Chat"/> is filestore-only — uploads and deletes never bump it.
/// </summary>
public static class ProjectFileKind
{
    public const string Original = "original";
    public const string Result = "result";

    /// <summary>The §12 persisted-chat JSON document (uploaded with <c>ext=json</c>).</summary>
    public const string Chat = "chat";
}
