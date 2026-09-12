using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The contract's §12.1 persisted-chat document: <see cref="ChatDocument.Build"/> (text-only,
/// role-filtered, trimmed to the most recent 32) and the tolerant <see cref="ChatDocument.TryParse"/>
/// (wrong version/shape ⇒ null — "no chat", never an error; stray images ignored).
/// </summary>
public sealed class ChatDocumentTests
{
    [Fact]
    public void BuildStripsImagesAndKeepsRolesAndTexts()
    {
        LlmImage image = new("image/png", "aGVsbG8=");
        ChatDocument doc = ChatDocument.Build(
            [
                new LlmMessage(LlmMessage.ROLE_USER, "crop it", [image]),
                new LlmMessage(LlmMessage.ROLE_ASSISTANT, "Done — anything else?"),
            ],
            savedAtMs: 1753900000000);

        Assert.Equal(1, doc.Version);
        Assert.Equal(1753900000000, doc.SavedAt);
        Assert.Equal(2, doc.Messages.Count);
        Assert.Equal(new ChatDocumentMessage("user", "crop it"), doc.Messages[0]);
        Assert.Equal(new ChatDocumentMessage("assistant", "Done — anything else?"), doc.Messages[1]);
        // The serialized document is text-only — the attachment never leaves the process.
        string json = doc.ToJson();
        Assert.DoesNotContain("images", json);
        Assert.DoesNotContain("aGVsbG8=", json);
    }

    [Fact]
    public void BuildDropsUnknownRolesAndTrimsToTheMostRecent32()
    {
        List<LlmMessage> messages = [new LlmMessage("system", "not persisted")];
        for (int i = 0; i < 40; i++)
        {
            messages.Add(new LlmMessage(LlmMessage.ROLE_USER, $"m{i}"));
        }

        ChatDocument doc = ChatDocument.Build(messages, savedAtMs: 1);

        Assert.Equal(ChatDocument.MAX_MESSAGES, doc.Messages.Count);
        Assert.Equal("m8", doc.Messages[0].Text);   // the oldest ones fell off the front
        Assert.Equal("m39", doc.Messages[^1].Text);
        Assert.DoesNotContain(doc.Messages, m => m.Role == "system");
    }

    [Fact]
    public void ToJsonMatchesTheContractShapeAndRoundTrips()
    {
        ChatDocument doc = ChatDocument.Build(
            [new LlmMessage(LlmMessage.ROLE_USER, "hi"), new LlmMessage(LlmMessage.ROLE_ASSISTANT, "hello")],
            savedAtMs: 42);

        string json = doc.ToJson();
        using JsonDocument parsed = JsonDocument.Parse(json);
        Assert.Equal(1, parsed.RootElement.GetProperty("version").GetInt32());
        Assert.Equal(42, parsed.RootElement.GetProperty("savedAt").GetInt64());
        Assert.Equal("user", parsed.RootElement.GetProperty("messages")[0].GetProperty("role").GetString());

        ChatDocument? back = ChatDocument.TryParse(json);
        Assert.NotNull(back);
        Assert.Equal(doc.Messages, back.Messages);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("not json at all")]
    [InlineData("[1,2,3]")]                                       // not an object
    [InlineData("{\"messages\":[]}")]                             // version absent
    [InlineData("{\"version\":2,\"messages\":[]}")]               // wrong version
    [InlineData("{\"version\":\"1\",\"messages\":[]}")]           // version not a number
    public void TryParseTreatsBadDocumentsAsMissing(string? json)
    {
        Assert.Null(ChatDocument.TryParse(json));
    }

    [Fact]
    public void TryParseDropsMalformedMessagesIgnoresStrayImagesAndUnknownFields()
    {
        const string json = """
            {
              "version": 1,
              "savedAt": 99,
              "surprise": true,
              "messages": [
                {"role": "user", "text": "keep me", "images": [{"data": "aGVsbG8="}]},
                {"role": "system", "text": "wrong role"},
                {"role": "assistant", "text": 5},
                {"role": "assistant"},
                "not an object",
                {"role": "assistant", "text": "kept too"}
              ]
            }
            """;

        ChatDocument? doc = ChatDocument.TryParse(json);

        Assert.NotNull(doc);
        Assert.Equal(99, doc.SavedAt);
        Assert.Equal(2, doc.Messages.Count);
        Assert.Equal(new ChatDocumentMessage("user", "keep me"), doc.Messages[0]);
        Assert.Equal(new ChatDocumentMessage("assistant", "kept too"), doc.Messages[1]);
        // The stray images were ignored on read and are never written back.
        Assert.DoesNotContain("images", doc.ToJson());
    }

    [Fact]
    public void TryParseTruncatesAnOverlongDocumentToTheMostRecent32()
    {
        string messages = string.Join(",", Enumerable.Range(0, 50)
            .Select(i => $"{{\"role\":\"user\",\"text\":\"m{i}\"}}"));

        ChatDocument? doc = ChatDocument.TryParse($"{{\"version\":1,\"messages\":[{messages}]}}");

        Assert.NotNull(doc);
        Assert.Equal(ChatDocument.MAX_MESSAGES, doc.Messages.Count);
        Assert.Equal("m18", doc.Messages[0].Text);
        Assert.Equal("m49", doc.Messages[^1].Text);
    }

    [Fact]
    public void TryParseToleratesAMissingSavedAtAndMissingMessages()
    {
        ChatDocument? doc = ChatDocument.TryParse("{\"version\":1}");

        Assert.NotNull(doc);
        Assert.Equal(0, doc.SavedAt);
        Assert.Empty(doc.Messages);
    }

    [Fact]
    public void BuildKeepsSection7MachineryOutOfTheDocument()
    {
        // The §7 continuation round restates the request with the internal note appended;
        // the shared document carries the user's own words, and never a raw op-plan as the
        // assistant's turn (§12.1) — though a user pasting JSON still sees their own text.
        ChatDocument doc = ChatDocument.Build(
            [
                new LlmMessage(LlmMessage.ROLE_USER, "blank a4, then crop it"),
                new LlmMessage(LlmMessage.ROLE_ASSISTANT, "made it"),
                new LlmMessage(LlmMessage.ROLE_USER, "blank a4, then crop it\n\n" + ChatDocument.CONTINUATION_NOTE),
                new LlmMessage(LlmMessage.ROLE_ASSISTANT, """{"version":1,"reply":"cropped it","actions":[]}"""),
                new LlmMessage(LlmMessage.ROLE_USER, """{"version":1,"actions":[]}"""),
            ],
            savedAtMs: 1);

        Assert.DoesNotContain("The working image is now", doc.ToJson());
        Assert.Equal(
            [
                new ChatDocumentMessage("user", "blank a4, then crop it"),
                new ChatDocumentMessage("assistant", "made it"),
                new ChatDocumentMessage("user", "blank a4, then crop it"),
                new ChatDocumentMessage("user", """{"version":1,"actions":[]}"""),
            ],
            doc.Messages);
    }

    [Fact]
    public void TryParseSanitizesADocumentWrittenByAnotherSurfaceOrAnOlderBuild()
    {
        // The document is shared: a note-only turn goes, an appended one is stripped back to
        // the request, and a raw op-plan assistant turn is refused — none of it may be shown
        // or replayed as if the user wrote or saw it.
        string json = $$"""
            {
              "version": 1,
              "messages": [
                {"role": "user", "text": "[The working image is now the frame you extracted — carry on.]"},
                {"role": "user", "text": "crop it\n\n{{ChatDocument.CONTINUATION_NOTE}}"},
                {"role": "assistant", "text": "{\"version\": 1, \"reply\": \"Cropped.\", \"actions\": []}"},
                {"role": "assistant", "text": "Cropped."}
              ]
            }
            """;

        ChatDocument? doc = ChatDocument.TryParse(json);

        Assert.NotNull(doc);
        Assert.Equal(
            [new ChatDocumentMessage("user", "crop it"), new ChatDocumentMessage("assistant", "Cropped.")],
            doc.Messages);
    }

    [Fact]
    public void ACleanDocumentRoundTripsIdentically()
    {
        ChatDocument built = ChatDocument.Build(
            [
                new LlmMessage(LlmMessage.ROLE_USER, "crop 10% off the left"),
                new LlmMessage(LlmMessage.ROLE_ASSISTANT, "Done — anything else?"),
            ],
            savedAtMs: 1753900000000);

        ChatDocument? reread = ChatDocument.TryParse(built.ToJson());

        Assert.NotNull(reread);
        Assert.Equal(built.SavedAt, reread.SavedAt);
        Assert.Equal(built.Messages, reread.Messages);
        Assert.Equal(built.ToJson(), reread.ToJson());
    }
}
