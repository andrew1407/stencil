using System.Collections.Concurrent;
using System.Text;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    public void ClearHistory(long userId) => _history.TryRemove(userId, out _);

    // History holds the RAW plan text; the §12.1 document carries the DISPLAYED reply, images
    // stripped.
    public ChatDocument? BuildChatDocument(long userId)
    {
        List<LlmMessage> history = snapshotHistory(userId);
        if (history.Count == 0)
        {
            return null;
        }
        List<LlmMessage> displayed = new(history.Count);
        foreach (LlmMessage message in history)
        {
            displayed.Add(message.Role == LlmMessage.RoleAssistant
                ? message with { Text = displayedReply(message.Text) }
                : message);
        }
        return ChatDocument.Build(displayed, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
    }

    private static string displayedReply(string raw) =>
        OpPlanParser.Parse(raw).Plan is OpPlan plan && plan.Reply.Length > 0 ? plan.Reply : raw;

    // §12: seeding never triggers a model call; text-only, re-gated at the point of USE.
    public int SeedHistory(long userId, ChatDocument doc)
    {
        UserHistory history = _history.GetOrAdd(userId, static _ => new UserHistory());
        history.Touched = Interlocked.Increment(ref _clock);
        int seeded;
        lock (history.Messages)
        {
            history.Messages.Clear();
            foreach (ChatDocumentMessage message in doc.Messages)
            {
                if (ChatDocument.DisplayText(message.Role, message.Text) is string shown)
                {
                    history.Messages.Add(new LlmMessage(message.Role, shown));
                }
            }
            seeded = history.Messages.Count;
        }
        evictIdleUsers(keep: userId);
        return seeded;
    }

    private List<LlmMessage> snapshotHistory(long userId)
    {
        if (!_history.TryGetValue(userId, out UserHistory? history))
        {
            return [];
        }
        history.Touched = Interlocked.Increment(ref _clock);
        lock (history.Messages)
        {
            return new List<LlmMessage>(history.Messages);
        }
    }

    private void recordTurn(long userId, string text, LlmImage? image, string assistantText)
    {
        UserHistory history = _history.GetOrAdd(userId, static _ => new UserHistory());
        history.Touched = Interlocked.Increment(ref _clock);
        List<LlmMessage> list = history.Messages;
        lock (list)
        {
            if (image is not null)
            {
                // §7 replays only the most recent prior image, so older base64 payloads are
                // dropped, not held.
                for (int i = 0; i < list.Count; i++)
                {
                    if (list[i].Images.Count > 0)
                    {
                        list[i] = list[i] with { Images = [] };
                    }
                }
            }
            list.Add(new LlmMessage(LlmMessage.RoleUser, text, image is null ? [] : [image]));
            list.Add(new LlmMessage(LlmMessage.RoleAssistant, assistantText));
            if (list.Count > MaxHistoryMessages)
            {
                list.RemoveRange(0, list.Count - MaxHistoryMessages);
            }
        }
        evictIdleUsers(keep: userId);
    }

    // Drops the least-recently-touched conversation, never the user being served.
    private void evictIdleUsers(long keep)
    {
        while (_history.Count > MaxTrackedUsers)
        {
            long oldestId = 0;
            long oldestTouch = long.MaxValue;
            bool found = false;
            foreach (KeyValuePair<long, UserHistory> entry in _history)
            {
                if (entry.Key != keep && entry.Value.Touched < oldestTouch)
                {
                    oldestTouch = entry.Value.Touched;
                    oldestId = entry.Key;
                    found = true;
                }
            }
            if (!found || !_history.TryRemove(oldestId, out _))
            {
                return;
            }
        }
    }
}
