namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// One media file the CLI downloaded during a source-site scrape: its on-disk path plus the
/// measured pixel dimensions when the CLI could sniff them. Parsed from a <c>wrote {path}
/// ({w}x{h} px · source {host})</c> stderr line (see the DESIGN source-site contract §3).
/// </summary>
/// <remarks>
/// <see cref="Width"/>/<see cref="Height"/> are null for a video or any item the CLI could not
/// measure (its <c>wrote</c> line carries no leading <c>WxH</c>). A file with dimensions is an
/// image (sent as a photo); one without is sent as a document.
/// </remarks>
public sealed record ScrapedFile(string Path, int? Width, int? Height);
