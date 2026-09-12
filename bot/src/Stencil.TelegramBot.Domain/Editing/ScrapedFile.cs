namespace Stencil.TelegramBot.Domain.Editing;

// Parsed from a `wrote {path} ({w}x{h} px · source {host})` stderr line. Null Width/Height (a
// video, or unmeasurable) routes the file to Telegram as a document rather than a photo.
public sealed record ScrapedFile(string Path, int? Width, int? Height);
