namespace Stencil.TelegramBot.Domain.Sessions;

// The closed set of UserSession.PendingInput values: the bot asked a question and is waiting
// on the next plain-text message. A slash command supersedes and clears any of them.
public static class PendingInputs
{
    public const string ExpiryDuration = "expiry";

    public const string ProjectName = "projectname";

    public const string ProjectDescription = "projectdescription";
}
