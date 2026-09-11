using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

// ServerService — the version-guarded single-field writes: colour, name, description, blank colour, expiry. Class doc lives in ServerService.cs.
public sealed partial class ServerService
{
    /// <inheritdoc />
    public async Task<string> SetProjectColorAsync(long userId, string color, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
        var client = ClientForActive(session);
        var record = await UpdateFieldWithRetryAsync(
            client,
            session.ActiveProjectId,
            v => new UpdateProjectRequest { Color = color, Version = v },
            "This project was edited elsewhere — reload it before changing its colour.",
            ct);
        var updated = session with { ActiveProjectVersion = record.Version };
        await _store.SaveAsync(updated, ct);
        return record.Color ?? "";
    }

    /// <inheritdoc />
    public async Task<string> SetProjectNameAsync(long userId, string name, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
        var trimmed = name.Trim();
        if (trimmed.Length == 0)
        {
            throw new InvalidOperationException("A project name can't be empty.");
        }
        var client = ClientForActive(session);
        var record = await UpdateFieldWithRetryAsync(
            client,
            session.ActiveProjectId,
            v => new UpdateProjectRequest { Name = trimmed, Version = v },
            "This project was edited elsewhere — reload it before renaming it.",
            ct);
        // The working-image label mirrors the project name (as /fetch seeds it), so update both.
        var updated = session with
        {
            ActiveProjectVersion = record.Version,
            ActiveProjectName = record.Name,
            ImageLabel = record.Name,
        };
        await _store.SaveAsync(updated, ct);
        return record.Name;
    }

    /// <inheritdoc />
    public async Task<string> SetProjectDescriptionAsync(long userId, string description, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
        var client = ClientForActive(session);
        var record = await UpdateFieldWithRetryAsync(
            client,
            session.ActiveProjectId,
            v => new UpdateProjectRequest { Description = description, Version = v },
            "This project was edited elsewhere — reload it before changing its description.",
            ct);
        var updated = session with
        {
            ActiveProjectVersion = record.Version,
            ActiveProjectDescription = record.Description ?? "",
        };
        await _store.SaveAsync(updated, ct);
        return record.Description ?? "";
    }

    /// <inheritdoc />
    public async Task<string> GetProjectBlankColorAsync(long userId, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
        var client = ClientForActive(session);
        var full = await client.GetProjectAsync(session.ActiveProjectId, ct);
        return full.Project.BlankColor ?? "";
    }

    /// <inheritdoc />
    public async Task<string> SetProjectBlankColorAsync(long userId, string color, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
        var client = ClientForActive(session);
        // Only a blank project has a blank colour; recolouring a non-blank is a no-op (empty result).
        var current = await client.GetProjectAsync(session.ActiveProjectId, ct);
        if (string.IsNullOrEmpty(current.Project.BlankColor))
        {
            return "";
        }
        var record = await UpdateFieldWithRetryAsync(
            client,
            session.ActiveProjectId,
            v => new UpdateProjectRequest { BlankColor = color, Version = v },
            "This project was edited elsewhere — reload it before changing its blank colour.",
            ct);
        var updated = session with { ActiveProjectVersion = record.Version };
        await _store.SaveAsync(updated, ct);
        return record.BlankColor ?? "";
    }

    /// <inheritdoc />
    public async Task<long> SetProjectExpiryAsync(long userId, long expiresAtMs, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
        var client = ClientForActive(session);
        // 0 means "keep forever": it is sent explicitly (not null) so the server clears any expiry.
        var record = await UpdateFieldWithRetryAsync(
            client,
            session.ActiveProjectId,
            v => new UpdateProjectRequest { ExpiresAt = expiresAtMs, Version = v },
            "This project was edited elsewhere — reload it before changing its expiry.",
            ct);
        var updated = session with { ActiveProjectVersion = record.Version, ActiveProjectExpiresAt = record.ExpiresAt };
        await _store.SaveAsync(updated, ct);
        return record.ExpiresAt;
    }
}
