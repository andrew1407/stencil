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

// PromptService — the per-user conversation registry: record/snapshot/seed/clear the
// bounded history, the §12.1 chat document, and idle-user eviction. Class doc lives
// in PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>Forget the user's chat history (e.g. when the working image is dropped).</summary>
    public void ClearHistory(long userId) => _history.TryRemove(userId, out _);

    /// <summary>
    /// Snapshot the conversation as the §12.1 persisted-chat document (null = nothing to persist).
    /// History holds the assistant's RAW plan text; the document carries the DISPLAYED reply, so
    /// each assistant entry is re-parsed. Images never enter it — <see cref="ChatDocument.Build"/> strips them.
    /// </summary>
    public ChatDocument? BuildChatDocument(long userId)
    {
        List<LlmMessage> history = SnapshotHistory(userId);
        if (history.Count == 0)
        {
            return null;
        }
        List<LlmMessage> displayed = new(history.Count);
        foreach (LlmMessage message in history)
        {
            displayed.Add(message.Role == LlmMessage.RoleAssistant
                ? message with { Text = DisplayedReply(message.Text) }
                : message);
        }
        return ChatDocument.Build(displayed, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
    }

    /// <summary>The §12.1 assistant text: the plan's <c>reply</c> when the raw text parses as one.</summary>
    private static string DisplayedReply(string raw) =>
        OpPlanParser.Parse(raw).Plan is OpPlan plan && plan.Reply.Length > 0 ? plan.Reply : raw;

    /// <summary>
    /// Replace the in-memory conversation with a restored persisted chat (§12: seeding never
    /// triggers a model call). Text-only, re-gated at the point of USE — an unparsed document
    /// can't seed §7 machinery as a real turn. Returns the number of seeded messages.
    /// </summary>
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
        EvictIdleUsers(keep: userId);
        return seeded;
    }

    private List<LlmMessage> SnapshotHistory(long userId)
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

    /// <summary>Append the user + assistant messages, keeping only the most recent 32.</summary>
    private void RecordTurn(long userId, string text, LlmImage? image, string assistantText)
    {
        UserHistory history = _history.GetOrAdd(userId, static _ => new UserHistory());
        history.Touched = Interlocked.Increment(ref _clock);
        List<LlmMessage> list = history.Messages;
        lock (list)
        {
            if (image is not null)
            {
                // The §7 replay rule only ever sends the most recent prior image, so older
                // turns' base64 payloads are dead weight — drop them instead of holding
                // megabytes per user. BuildTurn already stripped these from the wire shape.
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
        EvictIdleUsers(keep: userId);
    }

    /// <summary>
    /// Keep the registry bounded: while more than <see cref="MaxTrackedUsers"/> conversations
    /// are held, drop the least-recently-touched one (never the user being served).
    /// </summary>
    private void EvictIdleUsers(long keep)
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
