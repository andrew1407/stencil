namespace Stencil.TelegramBot.Domain.Llm;

// MediaType is the contract's accepted set: image/png, image/jpeg, image/webp, image/gif.
public sealed record LlmImage(string MediaType, string Base64Data)
{
    // §7: the long edge an attachment is downscaled to before base64-encoding.
    public const int MaxLongEdgePixels = 1568;
}
