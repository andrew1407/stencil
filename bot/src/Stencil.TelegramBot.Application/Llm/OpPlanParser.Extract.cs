using System.Text;
using System.Text.Json;

namespace Stencil.TelegramBot.Application.Llm;

public static partial class OpPlanParser
{
    private static string StripFences(string text)
    {
        if (!text.Contains("```"))
        {
            return text;
        }
        StringBuilder sb = new(text.Length);
        foreach (string line in text.Split('\n'))
        {
            if (line.TrimStart().StartsWith("```", StringComparison.Ordinal))
            {
                continue;
            }
            sb.Append(line).Append('\n');
        }
        return sb.ToString();
    }

    // String/escape-aware brace matching, retrying from each later { so stray braces in prose don't
    // hide an object.
    private static bool TryExtractJsonObject(string text, out JsonDocument? doc)
    {
        for (int start = text.IndexOf('{'); start >= 0; start = text.IndexOf('{', start + 1))
        {
            if (!TryFindBalancedEnd(text, start, out int end))
            {
                continue;
            }
            try
            {
                doc = JsonDocument.Parse(text[start..(end + 1)]);
                return true;
            }
            catch (JsonException)
            {
            }
        }
        doc = null;
        return false;
    }

    private static bool TryFindBalancedEnd(string text, int start, out int end)
    {
        int depth = 0;
        bool inString = false;
        for (int i = start; i < text.Length; i++)
        {
            char c = text[i];
            if (inString)
            {
                if (c == '\\')
                {
                    i++; // skip the escaped character
                }
                else if (c == '"')
                {
                    inString = false;
                }
                continue;
            }
            switch (c)
            {
                case '"': inString = true; break;
                case '{': depth++; break;
                case '}':
                    depth--;
                    if (depth == 0)
                    {
                        end = i;
                        return true;
                    }
                    break;
            }
        }
        end = -1;
        return false;
    }
}
