using System.Diagnostics;
using System.Text;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Xunit.Abstractions;
using Stencil.TelegramBot.Application.Llm.Plan;
using Stencil.TelegramBot.Tests.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>Timing tripwires for the bot's hot paths. Every assertion is RELATIVE — a ratio, or how one measurement scales as its input doubles — never a wall-clock ceiling.</summary>
[Trait("Category", "Bench")]
public sealed class BenchTests
{
    /// <summary>Reps per measurement: the best (fastest) one is kept, which drops scheduler noise.</summary>
    private const int _reps = 5;

    private readonly ITestOutputHelper _output;

    public BenchTests(ITestOutputHelper output) => _output = output;

    /// <summary>Best-of-<see cref="Reps"/> µs per invocation, after a warm-up pass.</summary>
    private double micros(string label, int iterations, Action body)
    {
        for (int i = 0; i < Math.Min(iterations, 200); i++)
        {
            body();
        }
        double best = double.MaxValue;
        for (int rep = 0; rep < _reps; rep++)
        {
            Stopwatch watch = Stopwatch.StartNew();
            for (int i = 0; i < iterations; i++)
            {
                body();
            }
            watch.Stop();
            best = Math.Min(best, watch.Elapsed.TotalMicroseconds / iterations);
        }
        _output.WriteLine($"{label}: {best:F3} µs/op (best of {_reps} x {iterations})");
        return best;
    }

    /// <summary>Report a ratio, then hold its ceiling — the assertion every test here makes.</summary>
    private void assertRatio(string label, double slower, double faster, double ceiling)
    {
        double ratio = slower / faster;
        _output.WriteLine($"  {label}: ratio {ratio:F2}x (ceiling {ceiling}x)");
        Assert.True(faster > 0.0, $"{label}: the baseline measured as zero");
        Assert.True(ratio < ceiling, $"{label} came out {ratio:F2}x, over the {ceiling}x ceiling");
    }

    [Fact]
    public void Should_Walk_The_Given_Layout_Once_In_The_Typed_Mapper()
    {
        string small = verdict(layout(lines: 40, points: 20));
        string wide = verdict(layout(lines: 80, points: 20));
        string deep = verdict(layout(lines: 40, points: 40));

        double baseline = micros("OpPlanParser.Map layout 40x20", 500, () => OpPlanParser.Map(small));
        double twiceTheLines = micros("OpPlanParser.Map layout 80x20", 250, () => OpPlanParser.Map(wide));
        double twiceThePoints = micros("OpPlanParser.Map layout 40x40", 250, () => OpPlanParser.Map(deep));

        // The mapping is one pass over lines x points; 2x the work is ~2x the time either way.
        assertRatio("twice the lines", twiceTheLines, baseline, ceiling: 3.0);
        assertRatio("twice the points", twiceThePoints, baseline, ceiling: 3.0);
    }

    [Fact]
    public void Should_Resolve_A_Crop_Spec_In_Time_Linear_In_Spec_Length()
    {
        // Repeated keys are legal (the last wins), so this grows the tokenizer's input honestly.
        string shortSpec = repeat("x1=10% x2=90% y1=10% y2=90% ", 5);
        string longSpec = repeat("x1=10% x2=90% y1=10% y2=90% ", 40);

        double small = micros("CropSpecResolver.Resolve (20 tokens)", 20_000,
            () => CropSpecResolver.Resolve(shortSpec, 4000, 3000, album: false));
        double large = micros("CropSpecResolver.Resolve (160 tokens)", 5_000,
            () => CropSpecResolver.Resolve(longSpec, 4000, 3000, album: false));

        // 8x the input: linear is 8x, so 16x catches a quadratic tokenizer (string concat).
        assertRatio("8x the spec length", large, small, ceiling: 16.0);
    }

    [Fact]
    public void Should_Reject_A_Crop_Spec_No_Dearer_Than_Resolving_One()
    {
        const string valid = "x1=10% x2=90% y1=10% y2=90%";
        const string malformed = "x1=10% x2=!!!!% y1=10% y2=90%";

        double accepted = micros("CropSpecResolver.Resolve (valid)", 50_000,
            () => CropSpecResolver.Resolve(valid, 4000, 3000, album: false));
        double rejected = micros("CropSpecResolver.Resolve (malformed)", 50_000,
            () => CropSpecResolver.Resolve(malformed, 4000, 3000, album: false));

        // The reject path returns null; this is what fails if it ever throws instead.
        assertRatio("malformed vs valid", rejected, accepted, ceiling: 3.0);
    }

    [Fact]
    public void Should_Ignore_The_Body_Behind_The_Header_When_Reading_An_Image_Header()
    {
        byte[][] headers =
        [
            ImageDimensionReaderTests.Png(1920, 1080),
            ImageDimensionReaderTests.Gif(640, 480),
            ImageDimensionReaderTests.Jpeg(3000, 2000),
            ImageDimensionReaderTests.WebpLossy(800, 600),
        ];
        byte[][] padded = [.. headers.Select(h => pad(h, 256 * 1024))];
        int index = 0;

        double bare = micros("ImageDimensionReader.TryRead (header only)", 200_000,
            () => ImageDimensionReader.TryRead(headers[index++ % headers.Length], out _, out _));
        index = 0;
        double withBody = micros("ImageDimensionReader.TryRead (+256 KiB body)", 200_000,
            () => ImageDimensionReader.TryRead(padded[index++ % padded.Length], out _, out _));

        // The reader's whole point: 256 KiB of untouched body must cost the same as none, so a
        // scan of it would show up here as hundreds of x.
        assertRatio("256 KiB body vs none", withBody, bare, ceiling: 3.0);
    }

    // Core's verdict on a one-action plan, as --plan-check prints its result.
    private static string verdict(string action) =>
        $$"""{"status":"valid","reply":"x","actions":[{{action}}],"variants":[],"ask":null,"warnings":[],"error":null}""";

    private static string layout(int lines, int points)
    {
        StringBuilder sb = new("""{"op":"layout","lines":[""");
        for (int line = 0; line < lines; line++)
        {
            sb.Append(line == 0 ? "" : ",").Append("""{"color":"#FFFF00","thickness":2,"points":[""");
            for (int point = 0; point < points; point++)
            {
                sb.Append(point == 0 ? "" : ",").Append($"{{\"x\":{point},\"y\":{line}}}");
            }
            sb.Append("]}");
        }
        return sb.Append("]}").ToString();
    }

    private static string repeat(string token, int times)
    {
        StringBuilder sb = new(token.Length * times);
        for (int i = 0; i < times; i++)
        {
            sb.Append(token);
        }
        return sb.ToString().TrimEnd();
    }

    private static byte[] pad(byte[] header, int total)
    {
        byte[] padded = new byte[Math.Max(total, header.Length)];
        header.CopyTo(padded, 0);
        return padded;
    }
}
