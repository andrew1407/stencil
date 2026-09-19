using Telegram.Bot;
using Telegram.Bot.Args;
using Telegram.Bot.Exceptions;
using Telegram.Bot.Requests.Abstractions;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>Records every outbound request object — the <c>Send*</c> extension methods all build an <see cref="IRequest{TResponse}"/> and call <see cref="SendRequest{TResponse}"/> — so tests inspect <see cref="Requests"/> and no Telegram network is touched.</summary>
public class MockBotClient : ITelegramBotClient
{
    /// <summary>Every request the bot handed to <see cref="SendRequest{TResponse}"/>, in order.</summary>
    public List<object> Requests { get; } = new();

    public bool LocalBotServer => false;

    public long BotId => 0;

    public TimeSpan Timeout { get; set; } = TimeSpan.FromSeconds(30);

    public IExceptionParser ExceptionsParser { get; set; } = default!;

    public event AsyncEventHandler<ApiRequestEventArgs>? OnMakingApiRequest;

    public event AsyncEventHandler<ApiResponseEventArgs>? OnApiResponseReceived;

    private int _nextMessageId;

    public virtual Task<TResponse> SendRequest<TResponse>(IRequest<TResponse> request, CancellationToken cancellationToken = default)
    {
        Requests.Add(request);
        // File-info lookups need a real TGFile back (GetInfoAndDownloadFile reads its FilePath).
        if (request is global::Telegram.Bot.Requests.GetFileRequest getFile)
        {
            TGFile file = new() { FileId = getFile.FileId, FileUniqueId = getFile.FileId, FilePath = "files/" + getFile.FileId };
            return Task.FromResult((TResponse)(object)file);
        }
        // A sent message gets a real id back, so a caller that edits or deletes its own notice
        // (ProgressNotice) exercises that path here instead of null-guarding out of it.
        if (request is global::Telegram.Bot.Requests.SendMessageRequest)
        {
            Message sent = new() { Id = Interlocked.Increment(ref _nextMessageId) };
            return Task.FromResult((TResponse)(object)sent);
        }
        return Task.FromResult<TResponse>(default!);
    }

    public Task<bool> TestApi(CancellationToken cancellationToken = default) => Task.FromResult(true);

    public virtual Task DownloadFile(string filePath, Stream destination, CancellationToken cancellationToken = default) =>
        Task.CompletedTask;

    public virtual Task DownloadFile(TGFile file, Stream destination, CancellationToken cancellationToken = default) =>
        Task.CompletedTask;

    /// <summary>Suppress "event never used" warnings — the mock never raises them.</summary>
    private void touchEvents()
    {
        _ = OnMakingApiRequest;
        _ = OnApiResponseReceived;
    }
}
