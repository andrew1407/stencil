namespace Stencil.TelegramBot.Domain.Abstractions;

// Shrink-only rescaling for LLM vision attachments (llm-contract.md §7: ≤ 1568 px on the long
// edge, re-encoded as PNG/JPEG). The adapter shells out to ffmpeg, already a video-path dep.
public interface IImageDownscaler
{
    // Never upscales; null when ffmpeg is missing or the input is unreadable.
    Task<byte[]?> DownscaleToPngAsync(string path, int maxLongEdge, CancellationToken ct = default);
}
