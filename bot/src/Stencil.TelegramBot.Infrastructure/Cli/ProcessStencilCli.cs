using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Processes;
using Stencil.TelegramBot.Infrastructure.Workspace;

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
            throw StencilCliException.Deployment(
                "Couldn't save the result — please try that again.",
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
            throw StencilCliException.Deployment(
                "The image engine didn't produce a result. Please try again.",
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
            TempFiles.TryDelete(outPath);
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
        try
        {
            string bin = StencilCliLocator.FindCli(_options.CliPath);
            ProcessOutcome outcome = await ProcessRunner
                .RunAsync(bin, argv, _options.CliTimeout, ct, NoColor)
                .ConfigureAwait(false);
            return outcome switch
            {
                ProcessCompleted completed => new CliOutput(completed.ExitCode == 0, completed.Stderr),
                ProcessStartFailed failed => throw StencilCliException.Deployment(
                    StencilCliLocator.UnavailableMessage,
                    $"failed to run the stencil CLI ({bin}): {failed.Message}"),
                _ => throw new StencilCliException(
                    $"the stencil CLI timed out after {_options.CliTimeout.TotalSeconds:0}s and was terminated"),
            };
        }
        finally
        {
            _spawnGate.Release();
        }
    }

    /// <summary>The CLI prints ANSI-coloured errors unless told not to; parsing needs plain text.</summary>
    private static readonly IReadOnlyDictionary<string, string> NoColor =
        new Dictionary<string, string> { ["NO_COLOR"] = "1" };

    /// <summary>Raw capture from one CLI invocation.</summary>
    private readonly record struct CliOutput(bool Success, string Stderr);
}
