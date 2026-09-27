using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm.Plan;

public static partial class OpPlanParser
{
    // §11: core has validated the card; the bot keeps labels only and says what it can't show.
    private static AskCard? mapAsk(JsonElement r, IReadOnlyDictionary<int, string> droppedPreviews, List<string> warnings)
    {
        if (!r.TryGetProperty("ask", out JsonElement ask) || ask.ValueKind != JsonValueKind.Object)
        {
            return null;
        }
        List<AskOption> options = new();
        bool preview = false;
        int index = 0;
        foreach (JsonElement option in ask.GetProperty("options").EnumerateArray())
        {
            index++;
            if (option.TryGetProperty("image", out JsonElement image) && image.ValueKind == JsonValueKind.Object)
            {
                warnings.Add(askImageNote(image, index));
            }
            string label = str(option, "label") ?? "";
            if (droppedPreviews.TryGetValue(index, out string? dropped))
            {
                warnings.Add(dropped);
            }
            else if (misplacedInPreview(option) is string op)
            {
                warnings.Add($"Dropped the preview for ask option {index} (\"{label}\") — {misplaced(op, "variants or previews")}; the option is still offered");
            }
            else if (option.TryGetProperty("actions", out _))
            {
                preview = true;
            }
            options.Add(new AskOption(label));
        }
        if (preview)
        {
            warnings.Add("some options asked for a rendered preview, which this chat can't produce — they are listed by name");
        }
        return new AskCard(
            str(ask, "question") ?? "",
            str(ask, "mode") == "multi",
            boolean(ask, "allowCustom") ?? false,
            str(ask, "customLabel") ?? DefaultCustomLabel,
            options);
    }

    // §11.2: no image resolves here — a url would have Telegram fetch a host nobody chose.
    private static string askImageNote(JsonElement image, int index) =>
        image.TryGetProperty("scanIndex", out _)
            ? $"ask option {index} names a page-scan image, which this chat has no access to"
            : image.TryGetProperty("projectId", out _)
                ? $"ask option {index} names a stored project, which this chat can't preview"
                : $"ask option {index} names an image URL, which this chat does not fetch — the option is shown without a preview";

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
