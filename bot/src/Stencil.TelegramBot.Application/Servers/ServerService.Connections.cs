using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

// ServerService — connect / disconnect / list: a connection is a validated token + base URL on the session. Class doc lives in ServerService.cs.
public sealed partial class ServerService
{
    /// <inheritdoc />
    public async Task<ServerConnectionInfo> ConnectAsync(long userId, string url, string? token, bool verifyTls, CancellationToken ct = default)
    {
        // Invite links carry the token as a '#token=' fragment; an explicit token wins.
        (url, token) = InviteLink.Split(url, token);
        // The bot is open to any Telegram user, so vet the target before issuing any REST call:
        // localhost/LAN collaboration servers are intended, but link-local / cloud-metadata
        // (169.254.169.254, fe80::/10, …) hosts are an SSRF-only target and are rejected.
        await RemoteImageUrl.ValidateServerUrlAsync(url, ct);
        var session = await _store.GetAsync(userId, ct);
        var normalized = _factory.NormalizeUrl(url);
        // Reuse what an earlier connect to this origin proved about the credential, so a known
        // admin token skips the probe that can only 401 (browser handshake parity).
        var known = session.FindConnection(normalized)?.CredentialKind ?? CredentialKind.None;
        var client = _factory.Create(url, token, verifyTls, credential: null, known);
        var handshake = await client.ConnectAsync(token, ct);
        // credential = what the user supplied (may be the ADMIN token): kept beside the live
        // session token so a later stale-session re-mint survives the round-tripped record, with
        // the kind the handshake proved it to be.
        var info = new ServerConnectionInfo
        {
            Url = normalized,
            Token = handshake.Token,
            Credential = token ?? "",
            CredentialKind = handshake.CredentialKind,
            VerifyTls = verifyTls,
        };
        var connections = session.Connections
            .Where(c => c.Url != normalized)
            .Append(info)
            .ToList();
        var updated = session with { Connections = connections };
        await _store.SaveAsync(updated, ct);
        return info;
    }

    /// <inheritdoc />
    public async Task<bool> DisconnectAsync(long userId, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var connections = session.Connections.ToList();
        ServerConnectionInfo? removed;
        if (url is null)
        {
            removed = connections.Count == 0 ? null : connections[^1];
        }
        else
        {
            var normalized = _factory.NormalizeUrl(url);
            removed = session.FindConnection(normalized);
        }
        if (removed is null)
        {
            return false;
        }
        connections.Remove(removed);
        var updated = session with { Connections = connections };
        await _store.SaveAsync(updated, ct);
        return true;
    }

    /// <inheritdoc />
    public async Task<IReadOnlyList<ServerConnectionInfo>> ConnectionsAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        return session.Connections;
    }
}
