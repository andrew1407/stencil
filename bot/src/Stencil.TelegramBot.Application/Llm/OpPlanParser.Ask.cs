using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

public static partial class OpPlanParser
{
    // §11: the registry's ask schema; a card nobody can answer rejects the whole plan.
    private static AskCard? ParseAsk(JsonElement root, List<string> warnings)
    {
        if (!root.TryGetProperty("ask", out JsonElement ask) || ask.ValueKind == JsonValueKind.Null)
        {
            return null;
        }
        Schema.ValidateAsk(ask);
        JsonElement keys = Schema.AskKeys;
        string question = OptionalString(ask, keys, "question")!;
        bool multi = OptionalString(ask, keys, "mode") == "multi";
        bool allowCustom = OptionalBool(ask, "allowCustom") ?? false;
        string customLabel = OptionalString(ask, keys, "customLabel") ?? DefaultCustomLabel;
        JsonElement optionKeys = keys.GetProperty("options").GetProperty("items").GetProperty("fields");

        List<AskOption> options = new();
        bool droppedPreview = false;
        int index = 0;
        foreach (JsonElement optionElement in ask.GetProperty("options").EnumerateArray())
        {
            index++;
            string label = OptionalString(optionElement, optionKeys, "label")!;
            if (HasField(optionElement, "image"))
            {
                NoteAskImage(optionElement.GetProperty("image"), index, warnings);
            }
            if (HasField(optionElement, "actions"))
            {
                // §1: a preview with a top-level-only or settings op loses the PREVIEW; the option
                // and the plan stand.
                try
                {
                    ParseActionList(optionElement, "actions", new List<string>(), inVariant: true);
                    droppedPreview = true;
                }
                catch (MisplacedOpException ex)
                {
                    warnings.Add($"Dropped the preview for option {index}{Named(label)} — {ex.Message} and can't ride inside an ask option's preview.");
                }
            }
            options.Add(new AskOption(label));
        }
        if (droppedPreview)
        {
            warnings.Add("some options asked for a rendered preview, which this chat can't produce — they are listed by name");
        }
        return new AskCard(question, multi, allowCustom, customLabel, options);
    }

    // §11.2: no image resolves here — a url would have Telegram fetch a host nobody chose.
    private static void NoteAskImage(JsonElement image, int index, List<string> warnings)
    {
        warnings.Add(HasField(image, "scanIndex")
            ? $"ask option {index} names a page-scan image, which this chat has no access to"
            : HasField(image, "projectId")
                ? $"ask option {index} names a stored project, which this chat can't preview"
                : $"ask option {index} names an image URL, which this chat does not fetch — the option is shown without a preview");
    }

    // The picked labels joined, or the typed custom text, trimmed and capped (§11.3).
    public static string AskAnswerText(IEnumerable<string> pickedLabels, string? custom = null)
    {
        string typed = (custom ?? "").Trim();
        string answer = typed.Length > 0
            ? typed
            : string.Join(", ", pickedLabels.Where(static l => !string.IsNullOrWhiteSpace(l)).Select(static l => l.Trim()));
        return answer.Length > MaxAskAnswer ? answer[..MaxAskAnswer] : answer;
    }
}
