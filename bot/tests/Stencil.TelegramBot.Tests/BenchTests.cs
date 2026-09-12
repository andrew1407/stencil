using System.Diagnostics;
using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Xunit.Abstractions;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Timing tripwires for the bot's hot paths. Every assertion is RELATIVE — a ratio between two
/// measurements, or how one scales as its input doubles — never a wall-clock ceiling, so it
/// means the same on CI and on a loaded laptop. The µs/op are printed, not asserted.
/// </summary>
[Trait("Category", "Bench")]
public sealed class BenchTests
{
    /// <summary>Reps per measurement: the best (fastest) one is kept, which drops scheduler noise.</summary>
    private const int Reps = 5;

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
        for (int rep = 0; rep < Reps; rep++)
        {
            Stopwatch watch = Stopwatch.StartNew();
            for (int i = 0; i < iterations; i++)
            {
                body();
            }
            watch.Stop();
            best = Math.Min(best, watch.Elapsed.TotalMicroseconds / iterations);
        }
        _output.WriteLine($"{label}: {best:F3} µs/op (best of {Reps} x {iterations})");
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
    public void ValidateActionWalksTheLayoutItIsGivenOnce()
    {
        OpSchema schema = OpSchema.Bot;
        (JsonElement Action, OpEntry Entry) small = action(schema, layout(lines: 40, points: 20));
        (JsonElement Action, OpEntry Entry) wide = action(schema, layout(lines: 80, points: 20));
        (JsonElement Action, OpEntry Entry) deep = action(schema, layout(lines: 40, points: 40));

        double baseline = micros("ValidateAction layout 40x20", 500, () => validate(schema, small));
        double twiceTheLines = micros("ValidateAction layout 80x20", 250, () => validate(schema, wide));
        double twiceThePoints = micros("ValidateAction layout 40x40", 250, () => validate(schema, deep));

        // The walk is one pass over lines x points; 2x the work is ~2x the time either way.
        assertRatio("twice the lines", twiceTheLines, baseline, ceiling: 3.0);
        assertRatio("twice the points", twiceThePoints, baseline, ceiling: 3.0);
    }

    [Fact]
    public void ValidateActionOfTheScalarOpsStaysInOneBand()
    {
        OpSchema schema = OpSchema.Bot;
        (JsonElement, OpEntry) rotate = action(schema, """{"op":"rotate","dir":"right"}""");
        (JsonElement, OpEntry) crop = action(schema,
            """{"op":"crop","spec":{"x1":"10%","x2":"-10%","y1":"0","y2":"90px","aspect":"4:3"}}""");

        double cheapest = micros("ValidateAction rotate (1 key)", 50_000, () => validate(schema, rotate));
        double dearest = micros("ValidateAction crop (5 sub-keys)", 50_000, () => validate(schema, crop));

        // Both are fixed-key scalar ops, so the spread is the key count and nothing else: a
        // wider gap means a per-call cost crept in (a registry re-resolve, say).
        assertRatio("crop vs rotate", dearest, cheapest, ceiling: 10.0);
    }

    [Fact]
    public void ParsingTheBotCorpusScalesWithTheCaseCount()
    {
        string[] all = [.. OpPlanCorpus.All.Where(f => f.AppliesToBot).Select(f => f.InputText)];
        string[] half = [.. all.Take(all.Length / 2)];
        int index = 0;

        double whole = micros($"OpPlanParser.Parse ({all.Length} cases)", all.Length * 4,
            () => OpPlanParser.Parse(all[index++ % all.Length]));
        index = 0;
        double part = micros($"OpPlanParser.Parse ({half.Length} cases)", half.Length * 4,
            () => OpPlanParser.Parse(half[index++ % half.Length]));

        // Per-case cost, not per-corpus: halving the corpus must not change µs/op much.
        assertRatio("whole vs half corpus", Math.Max(whole, part), Math.Min(whole, part), ceiling: 2.5);
    }

    [Fact]
    public void CropSpecResolutionIsLinearInSpecLength()
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
    public void RejectingACropSpecIsNoDearerThanResolvingOne()
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
    public void ImageHeaderReadingIgnoresTheBodyBehindTheHeader()
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

    private static void validate(OpSchema schema, (JsonElement Action, OpEntry Entry) pair) =>
        schema.ValidateAction(pair.Action, pair.Entry);

    private static (JsonElement, OpEntry) action(OpSchema schema, string json)
    {
        JsonElement element = JsonDocument.Parse(json).RootElement;
        return (element, schema.Ops[element.GetProperty("op").GetString()!]);
    }

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
