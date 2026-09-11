namespace Stencil.TelegramBot.Domain.Editing;

// Parsed from the CLI's `updated server result …` / `created server project …` stderr lines
// (cli/CONTRACT.md §2.2) — a port of the Remote enum in mcp's outcome parser.
public abstract record RemoteDelivery
{
    private RemoteDelivery() { }

    // --remote-update: `updated server result for project {id} ({w}x{h})`.
    public sealed record Updated(string Id, int Width, int Height) : RemoteDelivery;

    // --remote: `created server project "{name}" ({id})`.
    public sealed record Created(string Name, string Id) : RemoteDelivery;
}
