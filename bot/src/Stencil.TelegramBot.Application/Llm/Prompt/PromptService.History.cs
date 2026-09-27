using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    public void ClearHistory(long userId) => _history.TryRemove(userId, out _);

    // History holds the RAW plan text; the §12.1 document carries the reply shown at turn time, images
    // stripped.
    public ChatDocument? BuildChatDocument(long userId)
    {
        List<Said> history = snapshotSaid(userId);
        if (history.Count == 0)
        {
            return null;
        }
        List<LlmMessage> displayed = [.. history.Select(static s => s.Message.Role == LlmMessage.ROLE_ASSISTANT
            ? s.Message with { Text = s.Shown }
            : s.Message)];
        return ChatDocument.Build(displayed, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
    }

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
                    history.Messages.Add(new Said(new LlmMessage(message.Role, shown), shown));
                }
            }
            seeded = history.Messages.Count;
        }
        evictIdleUsers(keep: userId);
        return seeded;
    }

    private List<LlmMessage> snapshotHistory(long userId) => [.. snapshotSaid(userId).Select(static s => s.Message)];

    private List<Said> snapshotSaid(long userId)
    {
        if (!_history.TryGetValue(userId, out UserHistory? history))
        {
            return [];
        }
        history.Touched = Interlocked.Increment(ref _clock);
        lock (history.Messages)
        {
            return new List<Said>(history.Messages);
        }
    }

    private void recordTurn(long userId, string text, LlmImage? image, string assistantText, string shownReply)
    {
        UserHistory history = _history.GetOrAdd(userId, static _ => new UserHistory());
        history.Touched = Interlocked.Increment(ref _clock);
        List<Said> list = history.Messages;
        lock (list)
        {
            if (image is not null)
            {
                // §7 replays only the most recent prior image, so older base64 payloads are
                // dropped, not held.
                for (int i = 0; i < list.Count; i++)
                {
                    if (list[i].Message.Images.Count > 0)
                    {
                        list[i] = list[i] with { Message = list[i].Message with { Images = [] } };
                    }
                }
            }
            list.Add(new Said(new LlmMessage(LlmMessage.ROLE_USER, text, image is null ? [] : [image]), text));
            list.Add(new Said(new LlmMessage(LlmMessage.ROLE_ASSISTANT, assistantText), shownReply));
            if (list.Count > MAX_HISTORY_MESSAGES)
            {
                list.RemoveRange(0, list.Count - MAX_HISTORY_MESSAGES);
            }
        }
        evictIdleUsers(keep: userId);
    }

    // Drops the least-recently-touched conversation, never the user being served.
    private void evictIdleUsers(long keep)
    {
        while (_history.Count > MAX_TRACKED_USERS)
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
