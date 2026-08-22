using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>
/// An <see cref="IServerService"/> that records the active-project save/rename calls the §2.1
/// <c>save</c> op makes plus the §10 connect/disconnect calls, and fails loudly on anything
/// else, so a test can assert exactly which server path a prompt took. Set
/// <see cref="FailWith"/> to make the save throw, <see cref="ConnectFailWith"/> the connect.
/// </summary>
public sealed class RecordingServerService : IServerService
{
    public List<string> Saves { get; } = new();
    public List<string> Renames { get; } = new();
    public List<(string Url, string? Token, bool VerifyTls)> Connects { get; } = new();
    public List<string?> Disconnects { get; } = new();
    public List<string> Descriptions { get; } = new();
    public List<string> ProjectColors { get; } = new();
    public List<string> BlankColors { get; } = new();

    /// <summary>When set, <see cref="SaveActiveProjectAsync"/> throws with this message.</summary>
    public string? FailWith { get; set; }

    /// <summary>When set, <see cref="ConnectAsync"/> throws with this message.</summary>
    public string? ConnectFailWith { get; set; }

    /// <summary>When set, <see cref="ConnectAsync"/> throws exactly this (the 401/auth path).</summary>
    public Exception? ConnectThrows { get; set; }

    /// <summary><see cref="DisconnectAsync"/>'s result; false = nothing matched.</summary>
    public bool DisconnectResult { get; set; } = true;

    /// <summary>When set, <see cref="SetProjectNameAsync"/> throws with this message (duplicate-name path).</summary>
    public string? RenameFailWith { get; set; }

    /// <summary><see cref="SetProjectBlankColorAsync"/>'s result; "" = "not a blank" (the miss path).</summary>
    public string BlankColorResult { get; set; } = "#ffffff";

    /// <summary>Projects returned by <see cref="ListProjectsAsync"/> for the context suffix (null = throw like before).</summary>
    public List<ServerProjectInfo>? Projects { get; set; }

    /// <summary>Connections returned by <see cref="ConnectionsAsync"/> (null = throw like before).</summary>
    public List<ServerConnectionInfo>? Connections { get; set; }

    public Task<ProjectRecord> SaveActiveProjectAsync(long userId, CancellationToken ct = default)
    {
        if (FailWith is string message)
        {
            throw new InvalidOperationException(message);
        }
        string name = Renames.Count > 0 ? Renames[^1] : "project";
        Saves.Add(name);
        return Task.FromResult(new ProjectRecord { Id = "p1", Name = name, Version = Saves.Count });
    }

    public Task<string> SetProjectNameAsync(long userId, string name, CancellationToken ct = default)
    {
        if (RenameFailWith is string message)
        {
            throw new InvalidOperationException(message);
        }
        Renames.Add(name);
        return Task.FromResult(name);
    }

    public Task<string> SetProjectDescriptionAsync(long userId, string description, CancellationToken ct = default)
    {
        Descriptions.Add(description);
        return Task.FromResult(description);
    }

    public Task<string> SetProjectColorAsync(long userId, string color, CancellationToken ct = default)
    {
        ProjectColors.Add(color);
        return Task.FromResult(color);
    }

    public Task<string> SetProjectBlankColorAsync(long userId, string color, CancellationToken ct = default)
    {
        if (BlankColorResult.Length > 0)
        {
            BlankColors.Add(color);
        }
        return Task.FromResult(BlankColorResult.Length > 0 ? color : "");
    }

    public Task<IReadOnlyList<ServerProjectInfo>> ListProjectsAsync(long userId, string? url, CancellationToken ct = default) =>
        Projects is null
            ? Fail<Task<IReadOnlyList<ServerProjectInfo>>>()
            : Task.FromResult<IReadOnlyList<ServerProjectInfo>>(Projects);

    private static T Fail<T>() => throw new NotSupportedException("this server call is not part of the §2.1 save path");

    public Task<ServerConnectionInfo> ConnectAsync(long userId, string url, string? token, bool verifyTls, CancellationToken ct = default)
    {
        if (ConnectThrows is Exception failure)
        {
            throw failure;
        }
        if (ConnectFailWith is string message)
        {
            throw new InvalidOperationException(message);
        }
        Connects.Add((url, token, verifyTls));
        return Task.FromResult(new ServerConnectionInfo { Url = url, Token = token ?? "", VerifyTls = verifyTls });
    }

    public Task<bool> DisconnectAsync(long userId, string? url, CancellationToken ct = default)
    {
        Disconnects.Add(url);
        return Task.FromResult(DisconnectResult);
    }
    public Task<IReadOnlyList<ServerConnectionInfo>> ConnectionsAsync(long userId, CancellationToken ct = default) =>
        Connections is null
            ? Fail<Task<IReadOnlyList<ServerConnectionInfo>>>()
            : Task.FromResult<IReadOnlyList<ServerConnectionInfo>>(Connections);
    public Task<UserSession> FetchAsync(long userId, string nameOrId, string? url, CancellationToken ct = default) => Fail<Task<UserSession>>();
    public Task<ProjectRecord> CreateProjectAsync(long userId, string? name, string? url, CancellationToken ct = default) => Fail<Task<ProjectRecord>>();
    public Task<string> GetProjectBlankColorAsync(long userId, CancellationToken ct = default) => Fail<Task<string>>();
    public Task<long> SetProjectExpiryAsync(long userId, long expiresAtMs, CancellationToken ct = default) => Fail<Task<long>>();
    public Task<string> DeleteActiveProjectAsync(long userId, CancellationToken ct = default) => Fail<Task<string>>();
    public Task<long?> ActiveServerVersionAsync(long userId, CancellationToken ct = default) => Fail<Task<long?>>();
    public Task<UserSession?> PullActiveAsync(long userId, CancellationToken ct = default) => Fail<Task<UserSession?>>();
    public Task SaveChatAsync(long userId, string chatJson, CancellationToken ct = default) => Fail<Task>();
    public Task<string?> LoadChatAsync(long userId, CancellationToken ct = default) => Fail<Task<string?>>();
    public Task DeleteChatAsync(long userId, CancellationToken ct = default) => Fail<Task>();
}
