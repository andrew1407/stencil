namespace Stencil.TelegramBot.Domain.Llm;

// A named LlmOptions the operator configured, offered by the /chatapi picker.
// A PICKER, deliberately: every endpoint comes from the operator's environment, never from a
// chat message. Letting a user type a base URL would make the bot fetch whatever host a message
// named, so the cli console's free-text /llm form has no equivalent here.
public sealed record LlmProfile
{
    // Lookup key: lowercase, no spaces. What the session stores and the button carries.
    public required string Name { get; init; }

    // What the picker shows; defaults to Name when unset.
    public required string Label { get; init; }

    public required LlmOptions Options { get; init; }

    // One line for the picker: provider, model, where it points.
    public string Summary()
    {
        string model = Options.Model.Length > 0 ? Options.Model : "default model";
        string where = Options.Provider == LlmOptions.ProviderStencilServer
            ? Options.ServerUrl ?? "your connected server"
            : Options.BaseUrl;
        return $"{Options.Provider} · {model} · {where}";
    }
}
