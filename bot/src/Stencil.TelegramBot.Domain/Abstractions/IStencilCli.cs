using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Domain.Abstractions;

/// <summary>
/// The pixel engine: a thin port over the Zig CLI (<c>cli/</c>), which wraps the shared
/// C++ <c>core/</c>. Every transform runs through here, so results match the browser,
/// desktop, CLI and Python front-ends by construction. Conceptually the .NET sibling of
/// <c>mcp/src/pipeline.rs</c>.
/// </summary>
public interface IStencilCli
{
    /// <summary>
    /// Run one edit (source → crop → rotate → layout → filter → encode) and return the
    /// written file's path and dimensions. Throws <see cref="Exceptions.StencilCliException"/>
    /// on failure.
    /// </summary>
    Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default);

    /// <summary>
    /// Read an image/video source's pixel dimensions (decode + re-encode once, since the CLI
    /// has no read-only metadata mode). <paramref name="input"/> is a path or http(s) URL.
    /// </summary>
    Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default);
}
