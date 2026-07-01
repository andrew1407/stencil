using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Tests.Fakes;

/// <summary>
/// An in-process <see cref="IStencilCli"/> stand-in: it records the last
/// <see cref="EditRequest"/> it was handed, writes a stub byte to the requested output path
/// (so callers that read the rendered file work), and returns a canned
/// <see cref="RenderResult"/> / <see cref="ImageSize"/>. No real CLI binary is involved.
/// </summary>
public sealed class FakeStencilCli : IStencilCli
{
    /// <summary>The most recent request passed to <see cref="EditAsync"/> (null until first call).</summary>
    public EditRequest? LastRequest { get; private set; }

    /// <summary>How many times <see cref="EditAsync"/> ran.</summary>
    public int EditCalls { get; private set; }

    /// <summary>The dimensions the fake reports for both edits and probes.</summary>
    public ImageSize CannedSize { get; set; } = new(640, 480);

    /// <summary>Capture the request, materialise the output file and return a canned result.</summary>
    public async Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default)
    {
        LastRequest = request;
        EditCalls++;
        await File.WriteAllBytesAsync(request.Output, new byte[] { 0x89, 0x50 }, ct);
        return new RenderResult(request.Output, CannedSize.Width, CannedSize.Height);
    }

    /// <summary>Return the canned dimensions for any probed source.</summary>
    public Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default) =>
        Task.FromResult(CannedSize);
}
