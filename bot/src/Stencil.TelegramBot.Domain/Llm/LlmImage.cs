namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// One attached image: its media type (<c>image/png</c>, <c>image/jpeg</c>, <c>image/webp</c>
/// or <c>image/gif</c> — the contract's accepted set) and its base64-encoded bytes.
/// </summary>
public sealed record LlmImage(string MediaType, string Base64Data)
{
    /// <summary>
    /// Contract §7: attached images are downscaled to at most this many pixels on the long
    /// edge before base64-encoding.
    /// </summary>
    public const int MaxLongEdgePixels = 1568;
}
