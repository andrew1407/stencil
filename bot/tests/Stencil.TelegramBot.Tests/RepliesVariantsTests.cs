using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Projects;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The bare-command variant lists in <see cref="Replies"/> — a command that needs arguments,
/// sent bare, replies with its possible values instead of failing (SPEC feature 3).
/// </summary>
public sealed class RepliesVariantsTests
{
    [Fact]
    public void FilterVariantsListEveryMode()
    {
        string text = Replies.FilterVariants();
        foreach (string mode in new[] { "bw", "sepia", "invert", "contour", "none" })
        {
            Assert.Contains($"{mode} — ", text);
        }
        Assert.Contains("/filter #ff5623", text); // the duotone-colour example
    }

    [Fact]
    public void ProjectsTextCapsALongListAndNotesTheOverflow()
    {
        // A server with far more projects than the cap must not overflow Telegram's 4096-char
        // message limit; the extra ones are called out, not silently dropped.
        int total = Replies.MaxProjectsListed + 12;
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
        Assert.Contains($"and {total - Replies.MaxProjectsListed} more", text); // overflow called out
        Assert.Contains("Project 0", text);
        Assert.DoesNotContain($"Project {total - 1} ", text);  // the last (over the cap) is not
    }

    [Fact]
    public void RotateVariantsListTheQuarterTurns()
    {
        string text = Replies.RotateVariants();
        Assert.Contains("/rotate 1 — 90° clockwise", text);
        Assert.Contains("/rotate 2 — 180°", text);
        Assert.Contains("/rotate -1 — 90° counter-clockwise", text);
    }

    [Fact]
    public void CropUsageNamesTheSpecVocabulary()
    {
        string text = Replies.CropUsage();
        Assert.Contains("x1= x2= y1= y2=", text);
        Assert.Contains("/crop x1=10% x2=90% y1=10% y2=90%", text);
        Assert.Contains("album", text);
    }

    [Fact]
    public void PageFormatListHasOneLinePerFormatPlusTheCustomHint()
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
    public void DescribeEditsIncludesThePageFormat()
    {
        Assert.Contains("page B5", Replies.DescribeEdits(new EditState { PageFormat = "B5" }));
        Assert.Contains(
            "page custom 10×15.5cm",
            Replies.DescribeEdits(new EditState { PageFormat = "custom", CustomPageWidth = 10, CustomPageHeight = 15.5 }));
        Assert.Equal("none", Replies.DescribeEdits(new EditState()));
    }
}
