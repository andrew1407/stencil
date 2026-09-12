using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

// A plan can only re-run a connection the USER already saved: it never introduces a host or carries
// a token.
public sealed partial class PromptService
{
    internal Task ChangeConnectionAsync(ActionContext ctx, PlanAction action, CancellationToken ct) =>
        action is DisconnectAction disconnect
            ? DisconnectServerAsync(ctx, disconnect, ct)
            : ConnectServerAsync(ctx, (ConnectAction)action, ct);

    // The STORED token rides along; an unknown/ambiguous/refusing server is a warning, never a
    // failed plan.
    private async Task ConnectServerAsync(ActionContext ctx, ConnectAction connect, CancellationToken ct)
    {
        if (_projects is null)
        {
            ctx.Warnings.Add("Skipped connect — this bot has no server support.");
            return;
        }
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        ServerConnectionInfo? saved = ResolveConnection(connect.Server, session.Connections, out bool ambiguous);
        if (saved is null)
        {
            ctx.Warnings.Add(ambiguous
                ? $"Skipped connect — \"{Shown(connect.Server)}\" matches several of your connections; use the full URL."
                : $"Skipped connect — \"{Shown(connect.Server)}\" is not a server you saved; connect it first with /connect <url>.");
            return;
        }
        try
        {
            await _projects.ConnectAsync(ctx.UserId, saved.Url, saved.Token.Length > 0 ? saved.Token : null, saved.VerifyTls, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped connect — {saved.Url} refused: {ex.Message}");
        }
    }

    // A server that isn't connected is a warning, never a failed plan.
    private async Task DisconnectServerAsync(ActionContext ctx, DisconnectAction disconnect, CancellationToken ct)
    {
        if (_projects is null)
        {
            ctx.Warnings.Add("Skipped disconnect — this bot has no server support.");
            return;
        }
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        ServerConnectionInfo? connected = ResolveConnection(disconnect.Server, session.Connections, out bool ambiguous);
        if (connected is null)
        {
            ctx.Warnings.Add(ambiguous
                ? $"Skipped disconnect — \"{Shown(disconnect.Server)}\" matches several of your connections; use the full URL."
                : $"Skipped disconnect — \"{Shown(disconnect.Server)}\" isn't a connected server.");
            return;
        }
        try
        {
            if (!await _projects.DisconnectAsync(ctx.UserId, connected.Url, ct))
            {
                ctx.Warnings.Add($"Skipped disconnect — {connected.Url} isn't a connected server.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped disconnect — {connected.Url}: {ex.Message}");
        }
    }

    // The browser's resolveServer: exact URL, else a UNIQUE host[:port]/hostname match among the
    // user's OWN connections.
    private static ServerConnectionInfo? ResolveConnection(
        string server, IReadOnlyList<ServerConnectionInfo> connections, out bool ambiguous)
    {
        ambiguous = false;
        string want = server.Trim();
        ServerConnectionInfo? exact = connections.FirstOrDefault(
            c => string.Equals(c.Url, want, StringComparison.OrdinalIgnoreCase));
        if (exact is not null)
        {
            return exact;
        }
        List<ServerConnectionInfo> matches = connections.Where(c =>
            Uri.TryCreate(c.Url, UriKind.Absolute, out Uri? url)
            && (string.Equals(url.Authority, want, StringComparison.OrdinalIgnoreCase)
                || string.Equals(url.Host, want, StringComparison.OrdinalIgnoreCase))).ToList();
        ambiguous = matches.Count > 1;
        return matches.Count == 1 ? matches[0] : null;
    }

    private static string Shown(string server)
    {
        string s = server.Trim();
        return s.Length <= MaxLabelChars ? s : s[..MaxLabelChars] + "…";
    }
}
