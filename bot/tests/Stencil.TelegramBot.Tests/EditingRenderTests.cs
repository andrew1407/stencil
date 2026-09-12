using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// What <see cref="EditingService"/> hands the CLI and how the working state walks: the render
/// request, drawn lines, undo/redo, video frames, the URL guard and the JSON export.
/// </summary>
public sealed class EditingRenderTests : EditingServiceTestBase
{
    [Fact]
    public async Task Should_Build_A_Request_Carrying_Edits_And_Layout_Path_On_Render()
    {
        UserSession seeded = await _service.BlankAsync(UserId, new BlankSpec());
        await _service.SetCropAsync(UserId, "x1=5% x2=95%", album: false);
        await _service.RotateAsync(UserId, 1);
        await _service.SetFilterAsync(UserId, "bw");
        StencilLayout layout = new()
        {
            Lines = [new LayoutLine { Points = [new LayoutPoint(0, 0), new LayoutPoint(1, 1)] }],
        };
        await _service.ApplyLayoutAsync(UserId, layout);

        RenderResult result = await _service.RenderAsync(UserId);

        EditRequest request = _cli.LastRequest!;
        Assert.Equal(seeded.OriginalImagePath, request.Input);
        Assert.Equal("x1=5% x2=95%", request.CropSpec);
        Assert.Equal(1, request.Rotate);
        Assert.Equal("bw", request.Filter);
        Assert.NotNull(request.LayoutPath);
        Assert.True(File.Exists(request.LayoutPath));
        Assert.True(File.Exists(result.Path));
    }

    [Fact]
    public async Task Should_Throw_On_Render_With_No_Image()
    {
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.RenderAsync(UserId));
    }

    [Fact]
    public async Task Should_Append_Lines_Styled_With_The_Pen_When_Drawing()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.ConfigurePenAsync(UserId, color: "#ff0000", thickness: 5, pointSize: 0, style: "dashed", fill: "#00ff00");

        await _service.AddLineAsync(UserId, [new LayoutPoint(0, 0), new LayoutPoint(10, 10)], closed: false);
        UserSession afterOpen = await _store.GetAsync(UserId);
        LayoutLine open = afterOpen.Edits.Layout!.Lines.Single();
        Assert.Equal("#ff0000", open.Color);
        Assert.Equal(5, open.Thickness);
        Assert.Equal("dashed", open.Style);
        Assert.False(open.Locked);
        Assert.Equal(LayoutLine.DEFAULT_FILL_COLOR, open.FillColor); // open lines are never filled

        await _service.AddLineAsync(UserId, [new LayoutPoint(0, 0), new LayoutPoint(10, 0), new LayoutPoint(10, 10)], closed: true);
        UserSession afterClosed = await _store.GetAsync(UserId);
        Assert.Equal(2, afterClosed.Edits.LineCount);
        LayoutLine closed = afterClosed.Edits.Layout!.Lines[1];
        Assert.True(closed.Locked);
        Assert.Equal("#00ff00", closed.FillColor);
        Assert.Equal(new LayoutPoint(0, 0), closed.Points[^1]); // first point repeated to close
    }

    [Fact]
    public async Task Should_Step_Back_Through_Edits_On_Undo()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.SetCropAsync(UserId, "x1=10%", album: false);
        await _service.SetFilterAsync(UserId, "bw");

        UserSession undo1 = await _service.UndoAsync(UserId);
        Assert.Null(undo1.Edits.Filter);
        Assert.Equal("x1=10%", undo1.Edits.CropSpec);

        UserSession undo2 = await _service.UndoAsync(UserId);
        Assert.Null(undo2.Edits.CropSpec);

        UserSession undo3 = await _service.UndoAsync(UserId);
        Assert.True(undo3.Edits.IsEmpty);           // nothing left to undo — no-op
    }

    [Fact]
    public async Task Should_Reapply_Undone_Edits_On_Redo_Until_A_New_Edit_Clears_It()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.SetFilterAsync(UserId, "bw");
        await _service.UndoAsync(UserId);

        UserSession redone = await _service.RedoAsync(UserId);
        Assert.Equal("bw", redone.Edits.Filter);

        await _service.UndoAsync(UserId);
        await _service.SetFilterAsync(UserId, "sepia");          // a NEW edit clears the redo stack
        UserSession after = await _service.RedoAsync(UserId);    // nothing to redo now
        Assert.Equal("sepia", after.Edits.Filter);
    }

    [Fact]
    public async Task Should_Remove_The_Last_Line_Then_Clear_Lines()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.AddLineAsync(UserId, [new LayoutPoint(0, 0), new LayoutPoint(1, 1)], closed: false);
        await _service.AddLineAsync(UserId, [new LayoutPoint(2, 2), new LayoutPoint(3, 3)], closed: false);

        UserSession afterRemove = await _service.RemoveLastLineAsync(UserId);
        Assert.Equal(1, afterRemove.Edits.LineCount);

        UserSession afterClear = await _service.ClearLinesAsync(UserId);
        Assert.Equal(0, afterClear.Edits.LineCount);
        Assert.Null(afterClear.Edits.Layout);
    }

    [Fact]
    public async Task Should_Remember_The_Source_On_Video_Frame_Extraction()
    {
        string video = Path.Combine(_root, "clip.mp4");
        Directory.CreateDirectory(_root);
        await File.WriteAllBytesAsync(video, new byte[] { 0x00, 0x01 });

        UserSession loaded = await _service.SetImageFromVideoAsync(UserId, video, frame: 2, "clip");
        Assert.True(loaded.HasImage);
        Assert.NotNull(loaded.VideoSourcePath);
        Assert.Equal(2, _cli.LastRequest!.Frame);
        Assert.Equal(loaded.VideoSourcePath, _cli.LastRequest!.Input);

        await _service.ExtractFrameAsync(UserId, frame: 5);
        Assert.Equal(5, _cli.LastRequest!.Frame);
        UserSession after = await _store.GetAsync(UserId);
        Assert.Equal(loaded.VideoSourcePath, after.VideoSourcePath); // same video reused
    }

    [Fact]
    public async Task Should_Throw_On_Extract_Frame_Without_A_Video()
    {
        await _service.BlankAsync(UserId, new BlankSpec()); // an image, not a video
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.ExtractFrameAsync(UserId, 0));
    }

    [Theory]
    [InlineData("http://127.0.0.1/secret.png")] // SSRF to loopback
    [InlineData("ftp://example.com/a.png")]      // non-http scheme
    [InlineData("/etc/passwd.png")]              // bare local path (LFI)
    public async Task Should_Reject_Unsafe_Sources_Without_Invoking_The_Cli_On_Set_Image_From_Url(string url)
    {
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => _service.SetImageFromUrlAsync(UserId, url, "img"));
        Assert.Equal(0, _cli.EditCalls); // the CLI never fetched it
    }

    [Fact]
    public async Task Should_Emit_Expected_Fields_On_Export_Layout_Json()
    {
        UserSession session = new()
        {
            UserId = UserId,
            OriginalWidth = 320,
            OriginalHeight = 240,
            Edits = new EditState { Filter = "sepia" },
        };
        string json = _service.ExportLayoutJson(session);
        Assert.Contains("\"imageWidth\": 320", json);
        Assert.Contains("\"imageHeight\": 240", json);
        Assert.Contains("\"imageFilter\": \"sepia\"", json); // canonical key (Phase 6)
        Assert.Contains("\"lines\"", json);
    }
}
