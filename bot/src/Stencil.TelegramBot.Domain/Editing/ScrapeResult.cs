namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// The outcome of a source-site scrape: the destination directory the CLI wrote into and the
/// files it downloaded. Parsed from the CLI's <c>wrote …</c> lines plus the
/// <c>scraped {n} file(s) from {host} into {dir}</c> summary (DESIGN source-site contract §3).
/// </summary>
public sealed record ScrapeResult(string Directory, IReadOnlyList<ScrapedFile> Files);
