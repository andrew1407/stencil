namespace Stencil.TelegramBot.Domain.Projects;

/// <summary>
/// Returned by <c>POST /projects/{id}/files/{kind}</c> (protocol <c>FileWriteResponse</c>):
/// the stored path and the dimensions the server recorded (it is codec-free, so the caller
/// passes width/height in).
/// </summary>
public sealed record FileWriteResult(string Path, int W, int H);

/// <summary>
/// The two file kinds the server's file endpoints accept (protocol <c>KindOriginal</c> /
/// <c>KindResult</c>).
/// </summary>
public static class ProjectFileKind
{
    public const string Original = "original";
    public const string Result = "result";
}
