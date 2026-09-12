namespace Stencil.TelegramBot.Domain.Sessions;

// The closed set of UserSession.PendingInput values; a slash command supersedes and clears any.
public static class PendingInputs
{
    public const string EXPIRY_DURATION = "expiry";

    public const string PROJECT_NAME = "projectname";

    public const string PROJECT_DESCRIPTION = "projectdescription";
}
