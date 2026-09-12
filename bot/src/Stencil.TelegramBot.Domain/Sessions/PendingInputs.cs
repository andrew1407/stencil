namespace Stencil.TelegramBot.Domain.Sessions;

// The closed set of UserSession.PendingInput values; a slash command supersedes and clears any.
public static class PendingInputs
{
    public const string ExpiryDuration = "expiry";

    public const string ProjectName = "projectname";

    public const string ProjectDescription = "projectdescription";
}
