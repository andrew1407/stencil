namespace Stencil.TelegramBot.Domain.Editing;

// The bot's shape of core/state/historyStack: the session persists the two stacks, not a
// cursor, so every step returns a NEW stack plus the snapshot to apply. Push truncates redo,
// as the core's does.
public readonly record struct HistoryStack<T>(IReadOnlyList<T> Done, IReadOnlyList<T> Undone)
{
    // Per side; older ones are dropped.
    public const int MaxEntries = 25;

    public bool CanUndo => Done.Count > 0;

    public bool CanRedo => Undone.Count > 0;

    // A fresh edit clears redo.
    public HistoryStack<T> Push(T current) => new(Bounded(Done.Append(current)), []);

    public (HistoryStack<T> Stack, T Restored) Undo(T current) =>
        (new(Done.Take(Done.Count - 1).ToList(), Bounded(Undone.Append(current))), Done[^1]);

    public (HistoryStack<T> Stack, T Restored) Redo(T current) =>
        (new(Bounded(Done.Append(current)), Undone.Take(Undone.Count - 1).ToList()), Undone[^1]);

    public static HistoryStack<T> Empty => new([], []);

    private static List<T> Bounded(IEnumerable<T> stack)
    {
        List<T> list = stack.ToList();
        return list.Count > MaxEntries ? list.Skip(list.Count - MaxEntries).ToList() : list;
    }
}
