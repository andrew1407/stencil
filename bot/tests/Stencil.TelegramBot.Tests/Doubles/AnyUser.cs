using System.Collections;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>
/// A set of Telegram user ids containing every id — how fixtures testing the handlers (rather
/// than the allowlist) open <c>BotOptions.AllowedUsers</c> without threading their own id
/// through. Test-only: production keeps a finite set, so "on for everyone" is unreachable.
/// </summary>
internal sealed class AnyUser : IReadOnlySet<long>
{
    public static readonly AnyUser Instance = new();

    private AnyUser() { }

    public bool Contains(long item) => true;

    // Non-zero so `AllowedUsers.Count == 0` ("nothing configured") reads false.
    public int Count => int.MaxValue;

    public bool IsProperSubsetOf(IEnumerable<long> other) => false;
    public bool IsProperSupersetOf(IEnumerable<long> other) => other.Any();
    public bool IsSubsetOf(IEnumerable<long> other) => false;
    public bool IsSupersetOf(IEnumerable<long> other) => true;
    public bool Overlaps(IEnumerable<long> other) => other.Any();
    public bool SetEquals(IEnumerable<long> other) => false;

    // Unbounded by nature: enumeration is not something a caller can meaningfully do.
    public IEnumerator<long> GetEnumerator() =>
        throw new NotSupportedException("AnyUser matches every id; it cannot be enumerated");

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
