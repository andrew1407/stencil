using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram.Intake;

// By MIME type, and failing that by a known file extension.
internal static class DocumentKinds
{
    public static bool IsImage(Document document)
    {
        if (document.MimeType is string mime && mime.StartsWith("image/", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
        string ext = ExtensionOf(document.FileName ?? "", "");
        return ext is ".png" or ".jpg" or ".jpeg" or ".gif" or ".bmp" or ".webp" or ".tif" or ".tiff";
    }

    public static bool IsVideo(Document document)
    {
        if (document.MimeType is string mime && mime.StartsWith("video/", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
        string ext = ExtensionOf(document.FileName ?? "", "");
        return ext is ".mp4" or ".mov" or ".webm" or ".mkv" or ".avi" or ".m4v";
    }

    public static string ExtensionOf(string name, string fallback)
    {
        string ext = Path.GetExtension(name);
        return ext.Length == 0 ? fallback : ext.ToLowerInvariant();
    }
}
