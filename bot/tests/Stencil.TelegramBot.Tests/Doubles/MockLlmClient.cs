using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>Records every <see cref="LlmChatRequest"/> and returns queued canned replies (falling back to a chat-only default), or throws a configured exception. No provider or network is touched.</summary>
public sealed class MockLlmClient : ILlmClient
{
    /// <summary>Every request passed to <see cref="ChatAsync"/>, in order.</summary>
    public List<LlmChatRequest> Requests { get; } = new();

    /// <summary>Replies dequeued one per call; when empty, <see cref="DefaultReply"/> is used.</summary>
    public Queue<LlmReply> CannedReplies { get; } = new();

    /// <summary>The fallback reply — a well-formed chat-only plan.</summary>
    public LlmReply DefaultReply { get; set; } =
        new("{\"version\":1,\"reply\":\"ok\",\"actions\":[],\"variants\":[]}");

    /// <summary>When set, every call throws this instead of replying.</summary>
    public Exception? Throw { get; set; }

    /// <summary>Stands in for a slow model so a test can stop the turn mid-flight: the call waits on its token, and is signalled once it is actually in progress.</summary>
    public bool BlockUntilCancelled { get; set; }

    /// <summary>Set once a <see cref="BlockUntilCancelled"/> call is in flight.</summary>
    public TaskCompletionSource InFlight { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

    /// <summary>When set, each call waits on it before replying — a slow model the test completes on demand.</summary>
    public TaskCompletionSource? Hold { get; set; }

    /// <summary>Released once per call as it starts, so tests can await N calls being in flight.</summary>
    public SemaphoreSlim Entered { get; } = new(0);

    public async Task<LlmReply> ChatAsync(LlmChatRequest request, CancellationToken ct = default)
    {
        lock (Requests)   // gate tests run turns concurrently
        {
            Requests.Add(request);
        }
        Entered.Release();
        if (Throw is not null)
        {
            throw Throw;
        }
        if (BlockUntilCancelled)
        {
            InFlight.TrySetResult();
            await Task.Delay(Timeout.Infinite, ct).ConfigureAwait(false);
        }
        if (Hold is TaskCompletionSource hold)
        {
            await hold.Task.WaitAsync(ct).ConfigureAwait(false);
        }
        return CannedReplies.Count > 0 ? CannedReplies.Dequeue() : DefaultReply;
    }
}
