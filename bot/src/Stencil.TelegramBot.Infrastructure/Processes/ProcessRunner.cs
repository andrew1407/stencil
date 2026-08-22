using System.Diagnostics;

namespace Stencil.TelegramBot.Infrastructure.Processes;

/// <summary>The outcome of one bounded process run (see <see cref="ProcessRunner.RunAsync"/>).</summary>
public abstract record ProcessOutcome;

/// <summary>The process ran to completion (any exit code), with stderr fully captured.</summary>
public sealed record ProcessCompleted(int ExitCode, string Stderr) : ProcessOutcome;

/// <summary>The executable would not start (missing / not runnable).</summary>
public sealed record ProcessStartFailed(string Message) : ProcessOutcome;

/// <summary>The per-invocation deadline elapsed and the process tree was killed.</summary>
public sealed record ProcessTimedOut : ProcessOutcome;

/// <summary>
/// The one bounded external-process scaffold shared by every adapter that shells out (the
/// stencil CLI, ffmpeg): spawn with an argv + optional environment, drain stdout/stderr, wait
/// under the caller's token linked with a per-invocation deadline, and kill the whole process
/// tree when either fires. What each outcome MEANS (exception vs. graceful degradation) stays
/// at the call sites.
/// </summary>
public static class ProcessRunner
{
    /// <summary>
    /// Run <paramref name="fileName"/> with <paramref name="argv"/> and capture stderr.
    /// Returns <see cref="ProcessCompleted"/> / <see cref="ProcessStartFailed"/> /
    /// <see cref="ProcessTimedOut"/>; caller cancellation propagates as
    /// <see cref="OperationCanceledException"/> like every other async path.
    /// </summary>
    public static async Task<ProcessOutcome> RunAsync(
        string fileName,
        IReadOnlyList<string> argv,
        TimeSpan timeout,
        CancellationToken ct,
        IReadOnlyDictionary<string, string>? environment = null)
    {
        ProcessStartInfo info = new()
        {
            FileName = fileName,
            UseShellExecute = false,
            RedirectStandardError = true,
            RedirectStandardOutput = true,
        };
        foreach (string arg in argv)
        {
            info.ArgumentList.Add(arg);
        }
        if (environment is not null)
        {
            foreach ((string key, string value) in environment)
            {
                info.Environment[key] = value;
            }
        }

        using Process process = new() { StartInfo = info };
        try
        {
            process.Start();
        }
        catch (Exception e)
        {
            return new ProcessStartFailed(e.Message);
        }

        // Link the caller's token with the per-invocation deadline: whichever fires first (caller
        // cancel or timeout) trips the same token, and the process tree is killed below.
        using var timeoutCts = new CancellationTokenSource(timeout);
        using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(ct, timeoutCts.Token);
        try
        {
            Task<string> stderrTask = process.StandardError.ReadToEndAsync(linkedCts.Token);
            Task<string> stdoutTask = process.StandardOutput.ReadToEndAsync(linkedCts.Token);
            await process.WaitForExitAsync(linkedCts.Token).ConfigureAwait(false);
            string stderr = await stderrTask.ConfigureAwait(false);
            await stdoutTask.ConfigureAwait(false);
            return new ProcessCompleted(process.ExitCode, stderr);
        }
        catch (OperationCanceledException)
        {
            // Cancel or timeout: kill the whole tree so the process (and any child it spawned)
            // doesn't linger and keep fetching/writing after we've given up.
            KillTree(process);
            if (timeoutCts.IsCancellationRequested && !ct.IsCancellationRequested)
            {
                return new ProcessTimedOut();
            }
            throw;
        }
    }

    /// <summary>Terminate a process and its descendants, ignoring the races where it already exited.</summary>
    private static void KillTree(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
        catch
        {
            // Already exited / not started / permission — nothing more we can do.
        }
    }
}
