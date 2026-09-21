using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Tests.Telegram.Messaging;

/// <summary>The rig-free half of <see cref="Replies"/>: tone glyphs, the colour dot, and the bare-command variant/usage lists (a command needing arguments, sent bare, replies with its possible values). The tones seen through the real handlers live in <see cref="ReplyTonesTests"/>.</summary>
public sealed class RepliesTests
{
    // ── tones ──

    [Theory]
    [InlineData(Replies.Tone.ERROR, "🔴")]
    [InlineData(Replies.Tone.WARNING, "🟡")]
    [InlineData(Replies.Tone.SUCCESS, "✅")]
    [InlineData(Replies.Tone.NOTICE, "ℹ️")]
    public void Should_Give_Each_Tone_Its_Own_Glyph(Replies.Tone tone, string expected)
    {
        Assert.Equal(expected, Replies.Glyph(tone));
        Assert.Equal($"{expected} hello", Replies.Tag(tone, "hello"));
    }

    [Fact]
    public void Should_Keep_The_Four_Glyphs_Distinct()
    {
        string[] glyphs =
        [
            Replies.Glyph(Replies.Tone.ERROR),
            Replies.Glyph(Replies.Tone.WARNING),
            Replies.Glyph(Replies.Tone.SUCCESS),
            Replies.Glyph(Replies.Tone.NOTICE),
        ];

        Assert.Equal(glyphs.Length, glyphs.Distinct().Count());
    }

    // A message that already opens with a glyph of its own keeps it — never two in a row.
    [Theory]
    [InlineData("🗑 Removed 'x' from the server.")]
    [InlineData("↑ synced to 'x' (v2).")]
    [InlineData("💾 Chat saving on — …")]
    [InlineData("🟡 already a warning")]
    public void Should_Never_Stack_A_Second_Glyph_On_Tag(string message)
    {
        Assert.Equal(message, Replies.Tag(Replies.Tone.SUCCESS, message));
        Assert.Equal(message, Replies.Tag(Replies.Tone.ERROR, message));
    }

    [Fact]
    public void Should_Give_Plain_Text_Its_Glyph()
    {
        Assert.Equal("🔴 Nope.", Replies.Tag(Replies.Tone.ERROR, "Nope."));
        // Punctuation is not a glyph — only a leading symbol rune counts as one.
        Assert.StartsWith("🟡 —", Replies.Tag(Replies.Tone.WARNING, "— careful"));
    }

    // ── the colour dot: hex → nearest coloured circle (Telegram can't tint text) ──

    [Theory]
    [InlineData("#ff0000", "🔴")]
    [InlineData("#22cc44", "🟢")]
    [InlineData("#2277dd", "🔵")]
    [InlineData("#7c3aed", "🟣")]
    [InlineData("#111111", "⚫")]
    [InlineData("#f0f0f0", "⚪")]
    [InlineData("#f00", "🔴")]      // 3-digit hex expands
    public void Should_Map_Hex_To_The_Nearest_Dot_In_Color_Dot(string hex, string expected) =>
        Assert.Equal(expected, Replies.ColorDot(hex));

    [Fact]
    public void Should_Yield_No_Dot_For_An_Unset_Colour()
    {
        Assert.Equal("", Replies.ColorDot(null));
        Assert.Equal("", Replies.ColorDot(""));
    }

    [Fact]
    public void Should_Fall_Back_To_A_Palette_For_A_Named_Colour()
    {
        // A CSS name we don't resolve still signals "has a colour".
        Assert.Equal("🎨", Replies.ColorDot("teal"));
    }

    // ── the bare-command variant lists ──

    [Fact]
    public void Should_List_Every_Mode_In_Filter_Variants()
    {
        string text = Replies.FilterVariants();
        foreach (string mode in new[] { "bw", "sepia", "invert", "contour", "none" })
        {
            Assert.Contains($"{mode} — ", text);
        }
        Assert.Contains("/filter #ff5623", text); // the duotone-colour example
    }

    [Fact]
    public void Should_List_The_Quarter_Turns_In_Rotate_Variants()
    {
        string text = Replies.RotateVariants();
        Assert.Contains("/rotate 1 — 90° clockwise", text);
        Assert.Contains("/rotate 2 — 180°", text);
        Assert.Contains("/rotate -1 — 90° counter-clockwise", text);
    }

    [Fact]
    public void Should_Name_The_Spec_Vocabulary_In_Crop_Usage()
    {
        string text = Replies.CropUsage();
        Assert.Contains("x1= x2= y1= y2=", text);
        Assert.Contains("/crop x1=10% x2=90% y1=10% y2=90%", text);
        Assert.Contains("album", text);
    }

    [Fact]
    public void Should_Have_One_Line_Per_Format_Plus_The_Custom_Hint_In_Page_Format_List()
    {
        string text = Replies.PageFormatList();
        Assert.Contains("A4 (21×29.7 cm)", text);
        Assert.Contains("B5 (17.6×25 cm)", text);
        Assert.Contains("C10 (2.8×4 cm)", text);
        Assert.Contains("/format custom <w> <h>", text);
        // Every one of the 33 named formats gets a "name (w×h cm)" line.
        int lines = text.Split('\n').Count(l => l.Contains("×") && l.Contains(" (") && l.TrimEnd().EndsWith(" cm)"));
        Assert.Equal(33, lines);
    }

    [Fact]
    public void Should_Include_The_Page_Format_In_Describe_Edits()
    {
        Assert.Contains("page B5", Replies.DescribeEdits(new EditState { PageFormat = "B5" }));
        Assert.Contains(
            "page custom 10×15.5cm",
            Replies.DescribeEdits(new EditState { PageFormat = "custom", CustomPageWidth = 10, CustomPageHeight = 15.5 }));
        Assert.Equal("none", Replies.DescribeEdits(new EditState()));
    }

    [Fact]
    public void Should_Cap_A_Long_List_And_Note_The_Overflow_In_Projects_Text()
    {
        // A server with far more projects than the cap must not overflow Telegram's 4096-char
        // message limit; the extra ones are called out, not silently dropped.
        int total = Replies.MAX_PROJECTS_LISTED + 12;
        List<ServerProjectInfo> projects = new();
        for (int i = 0; i < total; i++)
        {
            projects.Add(new ServerProjectInfo(
                new ProjectRecord { Id = $"p_{i}", Name = $"Project {i}", HasImage = true, ImageW = 100, ImageH = 80 },
                "http://localhost:8090"));
        }

        string text = Replies.ProjectsText(projects);

        Assert.True(text.Length <= 4096, $"message length {text.Length} exceeds Telegram's limit");
        Assert.Contains($"Projects ({total})", text);          // the true total is shown
        Assert.Contains($"and {total - Replies.MAX_PROJECTS_LISTED} more", text); // overflow called out
        Assert.Contains("Project 0", text);
        Assert.DoesNotContain($"Project {total - 1} ", text);  // the last (over the cap) is not
    }
}
