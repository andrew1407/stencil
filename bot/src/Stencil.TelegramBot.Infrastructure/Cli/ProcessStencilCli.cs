using System.Diagnostics;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Infrastructure.Cli;

/// <summary>
/// The pixel engine: locates the Zig CLI, spawns it (with <c>NO_COLOR=1</c>), and maps the
/// exit status + stderr into a structured result or a <see cref="StencilCliException"/>. A
/// faithful port of <c>mcp/src/pipeline.rs</c>. All pixel work happens in the CLI/core, so
/// output is identical to the browser, desktop, CLI and Python front-ends by construction.
/// </summary>
/// <remarks>
/// Every edit and probe is a separate OS process, so a burst of concurrent users could otherwise
/// spawn an unbounded pile of them. A process-wide semaphore (sized by
/// <see cref="BotOptions.MaxConcurrentCli"/>) caps how many run at once; excess spawns wait their
/// turn. This adapter is a DI singleton, so the gate is shared across all users.
/// </remarks>
public sealed class ProcessStencilCli : IStencilCli
{
    private readonly BotOptions _options;
    private readonly SemaphoreSlim _spawnGate;

    public ProcessStencilCli(BotOptions options)
    {
        _options = options;
        _spawnGate = new SemaphoreSlim(options.MaxConcurrentCli, options.MaxConcurrentCli);
    }

    /// <summary>
    /// Run one edit: validate, refuse to clobber an existing output unless
    /// <see cref="EditRequest.Overwrite"/>, spawn the CLI, and parse the <c>wrote</c> line.
    /// </summary>
    public async Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default)
    {
        if (!request.Overwrite && File.Exists(request.Output))
        {
            throw new StencilCliException(
                $"output '{request.Output}' already exists; pass overwrite=true to replace it");
        }

        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(request);
        CliOutput output = await SpawnAsync(argv, ct).ConfigureAwait(false);
        if (!output.Success)
        {
            throw new StencilCliException(CliOutcomeParser.ExtractErrors(output.Stderr));
        }

        RenderResult? wrote = CliOutcomeParser.ParseWrote(output.Stderr);
        if (wrote is null)
        {
            throw new StencilCliException(
                "the stencil CLI reported success but printed no 'wrote' line:\n" + output.Stderr.Trim());
        }
        return wrote;
    }

    /// <summary>
    /// Read a source's pixel dimensions by rendering it to a fresh throwaway PNG under the
    /// data directory and parsing the <c>wrote</c> line (the CLI has no read-only metadata mode).
    /// </summary>
    public async Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default)
    {
        Directory.CreateDirectory(_options.DataDir);
        string outPath = Path.Combine(_options.DataDir, $"stencil-probe-{Guid.NewGuid():N}.png");
        try
        {
            IReadOnlyList<string> argv = new[] { "-i", input, outPath };
            CliOutput output = await SpawnAsync(argv, ct).ConfigureAwait(false);
            if (!output.Success)
            {
                throw new StencilCliException(CliOutcomeParser.ExtractErrors(output.Stderr));
            }
            RenderResult? wrote = CliOutcomeParser.ParseWrote(output.Stderr);
            if (wrote is null)
            {
                throw new StencilCliException("could not determine the image dimensions from the CLI output");
            }
            return wrote.Size;
        }
        finally
        {
            TryDelete(outPath);
        }
    }

    /// <summary>
    /// Run one source-site scrape: build the <c>--source-site</c> argv, spawn the CLI (which
    /// fetches the page, filters its media and downloads the matches), and parse its multi-file
    /// stderr into a <see cref="ScrapeResult"/>. A non-zero exit (e.g. nothing matched) surfaces
    /// as a <see cref="StencilCliException"/> carrying the CLI's <c>error:</c> line.
    /// </summary>
    public async Task<ScrapeResult> ScrapeAsync(ScrapeRequest request, CancellationToken ct = default)
    {
        IReadOnlyList<string> argv = CliArgvBuilder.BuildScrapeArgv(request);
        CliOutput output = await SpawnAsync(argv, ct).ConfigureAwait(false);
        if (!output.Success)
        {
            throw new StencilCliException(CliOutcomeParser.ExtractErrors(output.Stderr));
        }
        return CliOutcomeParser.ParseScraped(output.Stderr);
    }

    /// <summary>
    /// Locate the CLI and run it with the given argv, capturing stderr. Bounded by
    /// <see cref="_spawnGate"/> so no more than <see cref="BotOptions.MaxConcurrentCli"/> processes
    /// run concurrently across the whole bot, and by <see cref="BotOptions.CliTimeout"/> so a
    /// slow/hung invocation is killed rather than pinning a scarce concurrency slot forever.
    /// </summary>
    private async Task<CliOutput> SpawnAsync(IReadOnlyList<string> argv, CancellationToken ct)
    {
        await _spawnGate.WaitAsync(ct).ConfigureAwait(false);
        // Link the caller's token with a per-invocation deadline: whichever fires first (caller
        // cancel or timeout) trips the same token, and we kill the process below.
        using var timeoutCts = new CancellationTokenSource(_options.CliTimeout);
        using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(ct, timeoutCts.Token);
        CancellationToken runCt = linkedCts.Token;
        try
        {
            string bin = StencilCliLocator.FindCli(_options.CliPath);
            ProcessStartInfo info = new()
            {
                FileName = bin,
                UseShellExecute = false,
                RedirectStandardError = true,
                RedirectStandardOutput = true,
            };
            foreach (string arg in argv)
            {
                info.ArgumentList.Add(arg);
            }
            info.Environment["NO_COLOR"] = "1";

            using Process process = new() { StartInfo = info };
            try
            {
                process.Start();
            }
            catch (Exception e)
            {
                throw new StencilCliException($"failed to run the stencil CLI ({bin}): {e.Message}");
            }

            try
            {
                Task<string> stderrTask = process.StandardError.ReadToEndAsync(runCt);
                Task<string> stdoutTask = process.StandardOutput.ReadToEndAsync(runCt);
                await process.WaitForExitAsync(runCt).ConfigureAwait(false);
                string stderr = await stderrTask.ConfigureAwait(false);
                await stdoutTask.ConfigureAwait(false);

                return new CliOutput(process.ExitCode == 0, stderr);
            }
            catch (OperationCanceledException)
            {
                // Cancel or timeout: kill the whole tree so the CLI (and any child it spawned,
                // e.g. ffmpeg) doesn't linger and keep fetching/writing after we've given up.
                KillTree(process);
                if (timeoutCts.IsCancellationRequested && !ct.IsCancellationRequested)
                {
                    throw new StencilCliException(
                        $"the stencil CLI timed out after {_options.CliTimeout.TotalSeconds:0}s and was terminated");
                }
                throw;
            }
        }
        finally
        {
            _spawnGate.Release();
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

    /// <summary>Best-effort cleanup of the throwaway probe file.</summary>
    private static void TryDelete(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch
        {
            // Ignore — a leftover temp file is harmless.
        }
    }

    /// <summary>Raw capture from one CLI invocation.</summary>
    private readonly record struct CliOutput(bool Success, string Stderr);
}
