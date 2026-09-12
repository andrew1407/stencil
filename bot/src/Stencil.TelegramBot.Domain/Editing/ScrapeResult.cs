namespace Stencil.TelegramBot.Domain.Editing;

// Parsed from the CLI's `wrote …` lines and its `scraped {n} file(s) from {host} into {dir}`
// summary (§3).
public sealed record ScrapeResult(string Directory, IReadOnlyList<ScrapedFile> Files);
