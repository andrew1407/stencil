using Microsoft.Extensions.Logging;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>
/// An <see cref="ILogger{T}"/> that keeps every formatted entry in memory, so tests can assert
/// on what the operator would read in the server log (nothing is written anywhere).
/// </summary>
public sealed class MockLogger<T> : ILogger<T>
{
    /// <summary>Every entry logged, in order.</summary>
    public List<(LogLevel Level, string Message)> Entries { get; } = new();

    /// <summary>The formatted text of every entry, in order.</summary>
    public IEnumerable<string> Messages => Entries.Select(e => e.Message);

    public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

    public bool IsEnabled(LogLevel logLevel) => true;

    public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception,
        Func<TState, Exception?, string> formatter) =>
        Entries.Add((logLevel, formatter(state, exception)));
}
