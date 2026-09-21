using Stencil.TelegramBot.Domain.Llm.Wire;
namespace Stencil.TelegramBot.Domain.Llm;

// A PICKER, deliberately: every endpoint comes from the operator's environment, never from a chat
// message, so the bot never fetches a host a message named.
public sealed record LlmProfile
{
    // Lookup key: lowercase, no spaces.
    public required string Name { get; init; }

    public required string Label { get; init; }

    public required LlmOptions Options { get; init; }

    public string Summary()
    {
        string model = Options.Model.Length > 0 ? Options.Model : "default model";
        string where = Options.Provider == LlmOptions.PROVIDER_STENCIL_SERVER
            ? Options.ServerUrl ?? "your connected server"
            : Options.BaseUrl;
        return $"{Options.Provider} · {model} · {where}";
    }
}
