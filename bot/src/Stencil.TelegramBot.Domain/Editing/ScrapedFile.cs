namespace Stencil.TelegramBot.Domain.Editing;

// From a `wrote {path} ({w}x{h} px · source {host})` line; null Width/Height (video, unmeasurable) goes as a document.
public sealed record ScrapedFile(string Path, int? Width, int? Height);
