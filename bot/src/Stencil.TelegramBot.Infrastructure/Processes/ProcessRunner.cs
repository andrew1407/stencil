using System.Diagnostics;
using System.Text;

namespace Stencil.TelegramBot.Infrastructure.Processes;

public abstract record ProcessOutcome;

public sealed record ProcessCompleted(int ExitCode, string Stderr, string Stdout) : ProcessOutcome;

public sealed record ProcessStartFailed(string Message) : ProcessOutcome;

public sealed record ProcessTimedOut : ProcessOutcome;

// The one bounded external-process scaffold (stencil CLI, ffmpeg); what each outcome MEANS stays at
// the call sites.
public static class ProcessRunner
{
    // Per stream, in chars: far above the largest --script-plan envelope, and the rest is drained unkept.
    public const int MAX_OUTPUT_CHARS = 4 * 1024 * 1024;

    // Caller cancellation propagates as OperationCanceledException; a timeout is a ProcessTimedOut
    // outcome.
    public static async Task<ProcessOutcome> RunAsync(
        string fileName,
        IReadOnlyList<string> argv,
        TimeSpan timeout,
        CancellationToken ct,
        IReadOnlyDictionary<string, string>? environment = null,
        string? workingDirectory = null,
        string? stdin = null)
    {
        ProcessStartInfo info = new()
        {
            FileName = fileName,
            UseShellExecute = false,
            RedirectStandardError = true,
            RedirectStandardOutput = true,
            RedirectStandardInput = stdin is not null,
            WorkingDirectory = workingDirectory ?? "",
        };
        if (stdin is not null)
        {
            info.StandardInputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
        }
        foreach (string arg in argv)
        {
            info.ArgumentList.Add(arg);
        }
        ChildEnvironment.Restrict(info.Environment);
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
            Task<string> stderrTask = ReadCappedAsync(process.StandardError, MAX_OUTPUT_CHARS, linkedCts.Token);
            Task<string> stdoutTask = ReadCappedAsync(process.StandardOutput, MAX_OUTPUT_CHARS, linkedCts.Token);
            if (stdin is not null)
            {
                await feedAsync(process.StandardInput, stdin, linkedCts.Token).ConfigureAwait(false);
            }
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

    // Keeps the head and drains the rest, so a chatty child never blocks on a full pipe.
    public static async Task<string> ReadCappedAsync(TextReader reader, int maxChars, CancellationToken ct)
    {
        StringBuilder text = new();
        char[] buffer = new char[8192];
        int read;
        while ((read = await reader.ReadAsync(buffer, ct).ConfigureAwait(false)) > 0)
        {
            text.Append(buffer, 0, Math.Min(read, maxChars - text.Length));
        }
        return text.ToString();
    }

    // A child that exits before reading everything closes the pipe; its exit code says why.
    private static async Task feedAsync(StreamWriter writer, string text, CancellationToken ct)
    {
        try
        {
            await writer.WriteAsync(text.AsMemory(), ct).ConfigureAwait(false);
            await writer.FlushAsync(ct).ConfigureAwait(false);
        }
        catch (IOException)
        {
        }
        finally
        {
            try { writer.Close(); } catch (IOException) { }
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
