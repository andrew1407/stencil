using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// How <see cref="UpdateRouter"/> classifies an uploaded Telegram document: by MIME type, and
/// failing that by a known file extension.
/// </summary>
internal static class DocumentKinds
{
    /// <summary>Treat a document as an image by MIME type or by a known image extension.</summary>
    public static bool IsImage(Document document)
    {
        if (document.MimeType is string mime && mime.StartsWith("image/", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
        string ext = ExtensionOf(document.FileName ?? "", "");
        return ext is ".png" or ".jpg" or ".jpeg" or ".gif" or ".bmp" or ".webp" or ".tif" or ".tiff";
    }

    /// <summary>Treat a document as a video by MIME type or by a known video extension.</summary>
    public static bool IsVideo(Document document)
    {
        if (document.MimeType is string mime && mime.StartsWith("video/", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
        string ext = ExtensionOf(document.FileName ?? "", "");
        return ext is ".mp4" or ".mov" or ".webm" or ".mkv" or ".avi" or ".m4v";
    }

    /// <summary>The lowercased extension of a file name, or a fallback when none is present.</summary>
    public static string ExtensionOf(string name, string fallback)
    {
        string ext = Path.GetExtension(name);
        return ext.Length == 0 ? fallback : ext.ToLowerInvariant();
    }
}
