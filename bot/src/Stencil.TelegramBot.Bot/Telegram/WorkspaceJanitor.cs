using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Background sweeper that keeps each user's scratch directory from growing without bound. Every
/// render/probe writes a fresh file, but only the current original image (and any source video)
/// stays referenced by the session — the rest are orphans the moment a newer render supersedes
/// them. This loop periodically deletes those orphans once they age past
/// <see cref="BotOptions.WorkspaceTtl"/>, while always keeping the session-referenced files.
/// </summary>
public sealed class WorkspaceJanitor
{
    private readonly IUserWorkspace _workspace;
    private readonly ISessionStore _store;
    private readonly BotOptions _options;
    private readonly ILogger<WorkspaceJanitor> _logger;

    public WorkspaceJanitor(
        IUserWorkspace workspace,
        ISessionStore store,
        BotOptions options,
        ILogger<WorkspaceJanitor> logger)
    {
        _workspace = workspace;
        _store = store;
        _options = options;
        _logger = logger;
    }

    /// <summary>Sweep on a cadence of half the TTL (floored at 5 minutes) until cancelled.</summary>
    public async Task RunAsync(CancellationToken ct)
    {
        TimeSpan interval = Max(TimeSpan.FromTicks(_options.WorkspaceTtl.Ticks / 2), TimeSpan.FromMinutes(5));
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await SweepAsync(ct);
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

    /// <summary>One pass: prune every on-disk user's orphaned artifacts older than the TTL.</summary>
    private async Task SweepAsync(CancellationToken ct)
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

    /// <summary>The larger of two spans (no <c>TimeSpan.Max</c> in the BCL).</summary>
    private static TimeSpan Max(TimeSpan a, TimeSpan b) => a >= b ? a : b;
}
