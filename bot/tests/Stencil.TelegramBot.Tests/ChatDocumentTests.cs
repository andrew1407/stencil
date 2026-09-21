using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests;

/// <summary>The contract's §12.1 persisted-chat document: <see cref="ChatDocument.Build"/> (text-only, role-filtered, trimmed to the most recent 32) and the tolerant TryParse, where a wrong version or shape is "no chat" rather than an error.</summary>
public sealed class ChatDocumentTests
{
    [Fact]
    public void Should_Strip_Images_And_Keep_Roles_And_Texts_On_Build()
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
    public void Should_Drop_Unknown_Roles_And_Trim_To_The_Most_Recent_32_On_Build()
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
    public void Should_Match_The_Contract_Shape_And_Round_Trip_On_To_Json()
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
    public void Should_Treat_Bad_Documents_As_Missing_On_Try_Parse(string? json)
    {
        Assert.Null(ChatDocument.TryParse(json));
    }

    [Fact]
    public void Should_Drop_Malformed_Messages_And_Ignore_Stray_Images_And_Unknown_Fields_On_Try_Parse()
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
    public void Should_Truncate_An_Overlong_Document_To_The_Most_Recent_32_On_Try_Parse()
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
    public void Should_Tolerate_A_Missing_Saved_At_And_Missing_Messages_On_Try_Parse()
    {
        ChatDocument? doc = ChatDocument.TryParse("{\"version\":1}");

        Assert.NotNull(doc);
        Assert.Equal(0, doc.SavedAt);
        Assert.Empty(doc.Messages);
    }

    [Fact]
    public void Should_Keep_Section_7_Machinery_Out_Of_The_Document_On_Build()
    {
        // The shared document carries the user's own words, never the §7 internal note nor a raw op-plan as the
        // assistant's turn (§12.1).
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
    public void Should_Sanitize_A_Document_Written_By_Another_Surface_Or_An_Older_Build_On_Try_Parse()
    {
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
    public void Should_Round_Trip_A_Clean_Document_Identically()
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
