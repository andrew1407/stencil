namespace Stencil.TelegramBot.Domain.Editing;

// Parsed from a `wrote {path} ({w}x{h} px · source {host})` stderr line. Width/Height are null
// for a video or anything the CLI could not measure — and that null is what routes the file to
// Telegram as a document rather than a photo.
public sealed record ScrapedFile(string Path, int? Width, int? Height);
