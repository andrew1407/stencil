namespace Stencil.TelegramBot.Domain.Sessions;

/// <summary>
/// The recognised values of <see cref="UserSession.PendingInput"/> — a small closed set of
/// "the bot asked a question and is waiting for the user's next plain-text message" states.
/// A slash command supersedes and clears any pending prompt.
/// </summary>
public static class PendingInputs
{
    /// <summary>Awaiting a free-text expiry duration for the active project (e.g. "3 days").</summary>
    public const string ExpiryDuration = "expiry";
}
