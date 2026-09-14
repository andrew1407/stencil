using System.Diagnostics;

namespace Stencil.TelegramBot.Infrastructure.Processes;

public abstract record ProcessOutcome;

public sealed record ProcessCompleted(int ExitCode, string Stderr, string Stdout) : ProcessOutcome;

public sealed record ProcessStartFailed(string Message) : ProcessOutcome;

public sealed record ProcessTimedOut : ProcessOutcome;

// The one bounded external-process scaffold (stencil CLI, ffmpeg); what each outcome MEANS stays at
// the call sites.
public static class ProcessRunner
{
    // Caller cancellation propagates as OperationCanceledException; a timeout is a ProcessTimedOut
    // outcome.
    public static async Task<ProcessOutcome> RunAsync(
        string fileName,
        IReadOnlyList<string> argv,
        TimeSpan timeout,
        CancellationToken ct,
        IReadOnlyDictionary<string, string>? environment = null,
        string? workingDirectory = null)
    {
        ProcessStartInfo info = new()
        {
            FileName = fileName,
            UseShellExecute = false,
            RedirectStandardError = true,
            RedirectStandardOutput = true,
            WorkingDirectory = workingDirectory ?? "",
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

        // Whichever fires first (caller cancel or deadline) trips the same token, and the tree is
        // killed below.
        using var timeoutCts = new CancellationTokenSource(timeout);
        using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(ct, timeoutCts.Token);
        try
        {
            Task<string> stderrTask = process.StandardError.ReadToEndAsync(linkedCts.Token);
            Task<string> stdoutTask = process.StandardOutput.ReadToEndAsync(linkedCts.Token);
            await process.WaitForExitAsync(linkedCts.Token).ConfigureAwait(false);
            string stderr = await stderrTask.ConfigureAwait(false);
            string stdout = await stdoutTask.ConfigureAwait(false);
            return new ProcessCompleted(process.ExitCode, stderr, stdout);
        }
        catch (OperationCanceledException)
        {
            // Kill the whole tree so a child the process spawned doesn't keep fetching/writing
            // after we've given up.
            killTree(process);
            if (timeoutCts.IsCancellationRequested && !ct.IsCancellationRequested)
            {
                return new ProcessTimedOut();
            }
            throw;
        }
    }

    private static void killTree(Process process)
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
        }
    }
}
