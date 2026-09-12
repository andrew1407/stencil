using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Processes;
using Stencil.TelegramBot.Infrastructure.Workspace;

namespace Stencil.TelegramBot.Infrastructure.Cli;

// A port of mcp/src/pipeline.rs: spawns the Zig CLI with NO_COLOR=1 and maps exit status + stderr
// into a result or a StencilCliException. A process-wide semaphore (BotOptions.MaxConcurrentCli)
// caps concurrent spawns; this adapter is a DI singleton, so the gate is shared across all users.
public sealed class ProcessStencilCli : IStencilCli
{
    private readonly BotOptions _options;
    private readonly SemaphoreSlim _spawnGate;

    public ProcessStencilCli(BotOptions options)
    {
        _options = options;
        _spawnGate = new SemaphoreSlim(options.MaxConcurrentCli, options.MaxConcurrentCli);
    }

    public async Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default)
    {
        if (!request.Overwrite && File.Exists(request.Output))
        {
            throw StencilCliException.Deployment(
                "Couldn't save the result — please try that again.",
                $"output '{request.Output}' already exists; pass overwrite=true to replace it");
        }

        // --confine-output refuses an ABSOLUTE destination, so the child runs in the output's own
        // folder and is given only the leaf; paths in its output come back relative and are
        // re-rooted below.
        (string dir, string leaf) = Confine(request.Output);
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(request with { Output = leaf });
        CliOutput output = await SpawnAsync(argv, ct, dir).ConfigureAwait(false);
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
        return wrote with { Path = Path.Combine(dir, wrote.Path) };
    }

    // A throwaway render under the data directory: the CLI has no read-only metadata mode.
    public async Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default)
    {
        Directory.CreateDirectory(_options.DataDir);
        string outPath = Path.Combine(_options.DataDir, $"stencil-probe-{Guid.NewGuid():N}.png");
        try
        {
            (string dir, string leaf) = Confine(outPath);
            IReadOnlyList<string> argv = new[] { "-i", input, "--confine-output", leaf };
            CliOutput output = await SpawnAsync(argv, ct, dir).ConfigureAwait(false);
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

    // A non-zero exit (e.g. nothing matched) surfaces as a StencilCliException carrying the CLI's
    // error: line.
    public async Task<ScrapeResult> ScrapeAsync(ScrapeRequest request, CancellationToken ct = default)
    {
        // Same confinement as an edit: the child runs in the destination's PARENT with the leaf
        // directory name.
        (string parent, string leaf) = Confine(request.OutputDir.TrimEnd('/', '\\'));
        IReadOnlyList<string> argv = CliArgvBuilder.BuildScrapeArgv(request with { OutputDir = leaf });
        CliOutput output = await SpawnAsync(argv, ct, parent).ConfigureAwait(false);
        if (!output.Success)
        {
            throw new StencilCliException(CliOutcomeParser.ExtractErrors(output.Stderr));
        }
        ScrapeResult scraped = CliOutcomeParser.ParseScraped(output.Stderr);
        return new ScrapeResult(
            Path.Combine(parent, scraped.Directory),
            [.. scraped.Files.Select(f => f with { Path = Path.Combine(parent, f.Path) })]);
    }

    // A bare name keeps the caller's own working directory.
    private static (string Dir, string Leaf) Confine(string destination)
    {
        string dir = Path.GetDirectoryName(destination) ?? "";
        return (dir.Length == 0 ? Directory.GetCurrentDirectory() : dir, Path.GetFileName(destination));
    }

    // Bounded by the spawn gate and by BotOptions.CliTimeout, so a hung run can't pin a scarce slot
    // forever.
    private async Task<CliOutput> SpawnAsync(IReadOnlyList<string> argv, CancellationToken ct, string workingDirectory)
    {
        await _spawnGate.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            string bin = StencilCliLocator.FindCli(_options.CliPath);
            ProcessOutcome outcome = await ProcessRunner
                .RunAsync(bin, argv, _options.CliTimeout, ct, NoColor, workingDirectory)
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

    // The CLI prints ANSI-coloured errors unless told not to.
    private static readonly IReadOnlyDictionary<string, string> NoColor =
        new Dictionary<string, string> { ["NO_COLOR"] = "1" };

    private readonly record struct CliOutput(bool Success, string Stderr);
}
