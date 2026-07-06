namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// A collaboration-server delivery the CLI performed after writing the local file: the result
/// was either written back into a fetched project (<see cref="Updated"/>, from
/// <c>--remote-update</c>) or pushed as a brand-new project (<see cref="Created"/>, from
/// <c>--remote</c>). Parsed from the CLI's <c>updated server result …</c> / <c>created server
/// project …</c> stderr lines — a faithful port of the <c>Remote</c> enum in
/// <c>mcp/src/outcome.rs</c> (see <c>cli/CONTRACT.md</c> §2.2).
/// </summary>
public abstract record RemoteDelivery
{
    private RemoteDelivery() { }

    /// <summary>
    /// From <c>--remote-update</c>: <c>updated server result for project {id} ({w}x{h})</c>.
    /// </summary>
    public sealed record Updated(string Id, int Width, int Height) : RemoteDelivery;

    /// <summary>From <c>--remote</c>: <c>created server project "{name}" ({id})</c>.</summary>
    public sealed record Created(string Name, string Id) : RemoteDelivery;
}
