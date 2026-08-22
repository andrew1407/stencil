namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// One selectable chat API — a named <see cref="LlmOptions"/> the operator configured, offered
/// to users by the <c>/chatapi</c> picker.
/// </summary>
/// <remarks>
/// Profiles are a PICKER, deliberately: the endpoint of every one of them comes from the
/// operator's environment, never from a chat message. A bot that let a user type its base URL
/// would issue HTTP requests to whatever host a message named — the thing
/// <c>.claude/rules/security.md</c> exists to prevent — so the free-text form the cli console's
/// <c>/llm</c> offers has no equivalent here.
/// </remarks>
public sealed record LlmProfile
{
    /// <summary>Lookup key: lowercase, no spaces. What the session stores and the button carries.</summary>
    public required string Name { get; init; }

    /// <summary>What the picker shows (defaults to <see cref="Name"/> when unset).</summary>
    public required string Label { get; init; }

    /// <summary>The provider configuration this profile selects.</summary>
    public required LlmOptions Options { get; init; }

    /// <summary>A one-line description for the picker: provider, model and where it points.</summary>
    public string Summary()
    {
        string model = Options.Model.Length > 0 ? Options.Model : "default model";
        string where = Options.Provider == LlmOptions.ProviderStencilServer
            ? Options.ServerUrl ?? "your connected server"
            : Options.BaseUrl;
        return $"{Options.Provider} · {model} · {where}";
    }
}
