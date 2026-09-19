using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>Records the last <see cref="EditRequest"/>, writes a stub byte to the requested output path (so callers that read the rendered file work), and returns a canned result. No real CLI binary is involved.</summary>
public sealed class MockStencilCli : IStencilCli
{
    // Variant renders run in parallel, so the recorded calls are guarded.
    private readonly List<EditRequest> _requests = [];

    /// <summary>Every request passed to <see cref="EditAsync"/>, in call order.</summary>
    public IReadOnlyList<EditRequest> Requests { get { lock (_requests) { return [.. _requests]; } } }

    /// <summary>The most recent request passed to <see cref="EditAsync"/> (null until first call).</summary>
    public EditRequest? LastRequest { get { lock (_requests) { return _requests.Count == 0 ? null : _requests[^1]; } } }

    /// <summary>How many times <see cref="EditAsync"/> ran.</summary>
    public int EditCalls { get { lock (_requests) { return _requests.Count; } } }

    /// <summary>The dimensions the mock reports for both edits and probes.</summary>
    public ImageSize CannedSize { get; set; } = new(640, 480);

    /// <summary>When set, <see cref="EditAsync"/> throws for any request this predicate matches.</summary>
    public Func<EditRequest, bool>? FailWhen { get; set; }

    /// <summary>When set, awaited inside every edit — a test can hold calls open to see them overlap.</summary>
    public Func<Task>? BeforeEdit { get; set; }

    /// <summary>Capture the request, materialise the output file and return a canned result.</summary>
    public async Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default)
    {
        lock (_requests)
        {
            _requests.Add(request);
        }
        if (BeforeEdit is not null)
        {
            await BeforeEdit();
        }
        if (FailWhen?.Invoke(request) == true)
        {
            throw new InvalidOperationException("canned CLI failure");
        }
        await File.WriteAllBytesAsync(request.Output, new byte[] { 0x89, 0x50 }, ct);
        return new RenderResult(request.Output, CannedSize.Width, CannedSize.Height);
    }

    /// <summary>How many times <see cref="ProbeAsync"/> ran (each is a whole CLI process).</summary>
    public int ProbeCalls => _probeCalls;

    private int _probeCalls;

    /// <summary>Return the canned dimensions for any probed source.</summary>
    public Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default)
    {
        Interlocked.Increment(ref _probeCalls);
        return Task.FromResult(CannedSize);
    }

    /// <summary>The most recent request passed to <see cref="ScrapeAsync"/> (null until first call).</summary>
    public ScrapeRequest? LastScrapeRequest { get; private set; }

    /// <summary>How many times <see cref="ScrapeAsync"/> ran.</summary>
    public int ScrapeCalls { get; private set; }

    /// <summary>The stub files a scrape materialises under the request's output directory; a null width/height stands in for a video or unmeasured item, and the default is one image plus one video.</summary>
    public List<(string Name, int? Width, int? Height)> ScrapeStubs { get; } = new()
    {
        ("logo.png", 200, 80),
        ("clip.mp4", null, null),
    };

    /// <summary>The (scriptPath, input) pair the last <see cref="ScriptPlanAsync"/> was given.</summary>
    public (string Script, string? Input)? LastScriptCall { get; private set; }

    /// <summary>Every script file the mock was handed, in call order — read before it is deleted.</summary>
    public List<string> ScriptTexts { get; } = new();

    /// <summary>What <see cref="ScriptPlanAsync"/> returns; an empty plan by default.</summary>
    public ScriptPlan CannedScriptPlan { get; set; } = new([], []);

    /// <summary>When set, <see cref="ScriptPlanAsync"/> throws it instead of returning a plan.</summary>
    public Exception? ScriptFailure { get; set; }

    /// <summary>Record the call (and the script's bytes) and hand back the canned plan.</summary>
    public async Task<ScriptPlan> ScriptPlanAsync(string scriptPath, string? input = null, CancellationToken ct = default)
    {
        LastScriptCall = (scriptPath, input);
        ScriptTexts.Add(await File.ReadAllTextAsync(scriptPath, ct));
        return ScriptFailure is null ? CannedScriptPlan : throw ScriptFailure;
    }

    public async Task<ScrapeResult> ScrapeAsync(ScrapeRequest request, CancellationToken ct = default)
    {
        LastScrapeRequest = request;
        ScrapeCalls++;
        Directory.CreateDirectory(request.OutputDir);
        // Mirrors the CLI's paging window: a set count selects filtered[Group*Count : +Count] (no group ⇒ page 0),
        // an absent count takes every stub.
        IEnumerable<(string Name, int? Width, int? Height)> selected = ScrapeStubs;
        if (request.Count is int count)
        {
            int group = request.Group ?? 0;
            selected = ScrapeStubs.Skip(group * count).Take(count);
        }
        List<ScrapedFile> files = new();
        foreach ((string name, int? width, int? height) in selected)
        {
            string path = Path.Combine(request.OutputDir, name);
            await File.WriteAllBytesAsync(path, new byte[] { 0x89, 0x50 }, ct);
            files.Add(new ScrapedFile(path, width, height));
        }
        return new ScrapeResult(request.OutputDir, files);
    }
}
