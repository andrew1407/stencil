using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Domain.Configuration;

namespace Stencil.TelegramBot.Bot.Telegram.Sync;

// Deletes scratch files no session references once they age past WorkspaceTtl; the referenced
// originals are kept.
public sealed class WorkspaceJanitor : BackgroundService
{
    private readonly IUserWorkspace _workspace;
    private readonly ISessionStore _store;
    private readonly IBotPolicy _options;
    private readonly ILogger<WorkspaceJanitor> _logger;

    public WorkspaceJanitor(
        IUserWorkspace workspace,
        ISessionStore store,
        IBotPolicy options,
        ILogger<WorkspaceJanitor> logger)
    {
        _workspace = workspace;
        _store = store;
        _options = options;
        _logger = logger;
    }

    // Half the TTL, floored at 5 minutes.
    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        TimeSpan interval = max(TimeSpan.FromTicks(_options.WorkspaceTtl.Ticks / 2), TimeSpan.FromMinutes(5));
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await sweepAsync(ct);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "Workspace sweep failed");
            }
            try
            {
                await Task.Delay(interval, ct);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private async Task sweepAsync(CancellationToken ct)
    {
        DateTime cutoffUtc = DateTime.UtcNow - _options.WorkspaceTtl;
        int total = 0;
        foreach (long userId in _workspace.ActiveUserIds())
        {
            ct.ThrowIfCancellationRequested();
            UserSession session = await _store.GetAsync(userId, ct);
            string[] keep = new[] { session.OriginalImagePath, session.VideoSourcePath }
                .Where(p => !string.IsNullOrEmpty(p))
                .Select(p => p!)
                .ToArray();
            total += _workspace.PruneStale(userId, keep, cutoffUtc);
        }
        if (total > 0)
        {
            _logger.LogInformation("Workspace sweep removed {Count} stale file(s)", total);
        }
    }

    private static TimeSpan max(TimeSpan a, TimeSpan b) => a >= b ? a : b;
}
