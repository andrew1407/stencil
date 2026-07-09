using Telegram.Bot;
using Telegram.Bot.Args;
using Telegram.Bot.Exceptions;
using Telegram.Bot.Requests.Abstractions;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Tests.Fakes;

/// <summary>
/// An in-process <see cref="ITelegramBotClient"/> stand-in that records every outbound request
/// object (the <c>Send*</c> extension methods all build an <see cref="IRequest{TResponse}"/> and
/// call <see cref="SendRequest{TResponse}"/>). Tests inspect <see cref="Requests"/> to assert what
/// the handler sent — no Telegram network is ever touched.
/// </summary>
public sealed class FakeBotClient : ITelegramBotClient
{
    /// <summary>Every request the bot handed to <see cref="SendRequest{TResponse}"/>, in order.</summary>
    public List<object> Requests { get; } = new();

    public bool LocalBotServer => false;

    public long BotId => 0;

    public TimeSpan Timeout { get; set; } = TimeSpan.FromSeconds(30);

    public IExceptionParser ExceptionsParser { get; set; } = default!;

    public event AsyncEventHandler<ApiRequestEventArgs>? OnMakingApiRequest;

    public event AsyncEventHandler<ApiResponseEventArgs>? OnApiResponseReceived;

    public Task<TResponse> SendRequest<TResponse>(IRequest<TResponse> request, CancellationToken cancellationToken = default)
    {
        Requests.Add(request);
        return Task.FromResult<TResponse>(default!);
    }

    public Task<bool> TestApi(CancellationToken cancellationToken = default) => Task.FromResult(true);

    public Task DownloadFile(string filePath, Stream destination, CancellationToken cancellationToken = default) =>
        Task.CompletedTask;

    public Task DownloadFile(TGFile file, Stream destination, CancellationToken cancellationToken = default) =>
        Task.CompletedTask;

    /// <summary>Suppress "event never used" warnings — the fake never raises them.</summary>
    private void TouchEvents()
    {
        _ = OnMakingApiRequest;
        _ = OnApiResponseReceived;
    }
}
