namespace Stencil.TelegramBot.Domain.Abstractions;

/// <summary>
/// Shrink-only image rescaling for LLM vision attachments (<c>llm-contract.md</c> §7:
/// attached images are downscaled to ≤ 1568 px on the long edge and re-encoded as PNG or
/// JPEG). The Infrastructure adapter shells out to <c>ffmpeg</c> — the same external tool the
/// video pipeline already relies on.
/// </summary>
public interface IImageDownscaler
{
    /// <summary>
    /// Re-encode the image at <paramref name="path"/> as a PNG whose long edge is at most
    /// <paramref name="maxLongEdge"/> px (never upscaling), and return the PNG bytes. Returns
    /// null when the rescale isn't possible (ffmpeg missing, unreadable input, failure) so the
    /// caller can degrade gracefully to the original bytes.
    /// </summary>
    Task<byte[]?> DownscaleToPngAsync(string path, int maxLongEdge, CancellationToken ct = default);
}
