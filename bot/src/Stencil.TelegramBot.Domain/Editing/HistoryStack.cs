namespace Stencil.TelegramBot.Domain.Editing;

// The bot's shape of core/state/historyStack: the session persists the two stacks, not a cursor, so
// every step returns a NEW stack plus the snapshot to apply.
public readonly record struct HistoryStack<T>(IReadOnlyList<T> Done, IReadOnlyList<T> Undone)
{
    // Per side; older ones are dropped.
    public const int MAX_ENTRIES = 25;

    public bool CanUndo => Done.Count > 0;

    public bool CanRedo => Undone.Count > 0;

    // A fresh edit clears redo.
    public HistoryStack<T> Push(T current) => new(bounded(Done.Append(current)), []);

    public (HistoryStack<T> Stack, T Restored) Undo(T current) =>
        (new(Done.Take(Done.Count - 1).ToList(), bounded(Undone.Append(current))), Done[^1]);

    public (HistoryStack<T> Stack, T Restored) Redo(T current) =>
        (new(bounded(Done.Append(current)), Undone.Take(Undone.Count - 1).ToList()), Undone[^1]);

    public static HistoryStack<T> Empty => new([], []);

    private static List<T> bounded(IEnumerable<T> stack)
    {
        List<T> list = stack.ToList();
        return list.Count > MAX_ENTRIES ? list.Skip(list.Count - MAX_ENTRIES).ToList() : list;
    }
}
