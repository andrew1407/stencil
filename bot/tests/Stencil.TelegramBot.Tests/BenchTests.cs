using System.Diagnostics;
using System.Text.Json;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Xunit.Abstractions;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Timing tripwires for the hot paths the suite leans on: op-plan validation (the fixture walk
/// runs it ~630 x 42 times), crop-spec resolution, image-header reading. Excluded from the
/// default run by the project's <c>Category!=Bench</c> filter. Ceilings are loose on purpose —
/// they catch an order-of-magnitude regression, not a noisy machine.
/// </summary>
[Trait("Category", "Bench")]
public sealed class BenchTests
{
    private readonly ITestOutputHelper _output;

    public BenchTests(ITestOutputHelper output) => _output = output;

    /// <summary>Warm up, time <paramref name="iterations"/> runs, report, then hold the ceiling.</summary>
    private void Measure(string label, int iterations, double ceilingMicros, Action body)
    {
        for (int i = 0; i < Math.Min(iterations, 100); i++)
        {
            body();
        }
        Stopwatch watch = Stopwatch.StartNew();
        for (int i = 0; i < iterations; i++)
        {
            body();
        }
        watch.Stop();
        double micros = watch.Elapsed.TotalMicroseconds / iterations;
        _output.WriteLine($"{label}: {micros:F2} µs/op over {iterations} ops (ceiling {ceilingMicros} µs)");
        Assert.True(micros < ceilingMicros, $"{label} took {micros:F2} µs/op, over the {ceilingMicros} µs ceiling");
    }

    [Fact]
    public void ValidateActionStaysCheap()
    {
        OpSchema schema = OpSchema.Bot;
        (JsonElement Action, OpEntry Entry)[] actions =
        [
            Action(schema, """{"op":"rotate","dir":"right"}"""),
            Action(schema, """{"op":"filter","mode":"sepia"}"""),
            Action(schema, """{"op":"crop","spec":{"x1":"10%","x2":"-10%"}}"""),
            Action(schema, """{"op":"page","format":"a4"}"""),
        ];

        Measure("OpSchema.ValidateAction", 20_000, ceilingMicros: 200, () =>
        {
            foreach ((JsonElement action, OpEntry entry) in actions)
            {
                schema.ValidateAction(action, entry);
            }
        });
    }

    [Fact]
    public void ParsingTheWholeBotCorpusStaysCheap()
    {
        // What one fixture walk costs end to end, per case.
        string[] inputs = [.. OpPlanCorpus.All.Where(f => f.AppliesToBot).Select(f => f.InputText)];
        int index = 0;

        Measure("OpPlanParser.Parse (corpus)", inputs.Length * 4, ceilingMicros: 400,
            () => OpPlanParser.Parse(inputs[index++ % inputs.Length]));
    }

    [Fact]
    public void CropSpecResolutionStaysCheap()
    {
        string[] specs =
        [
            "x1=10% x2=90% y1=10% y2=90%",
            "x1=100px",
            "y2=100px",
            "x1=1cm x2=5cm",
            "",
        ];
        int index = 0;

        Measure("CropSpecResolver.Resolve", 50_000, ceilingMicros: 50,
            () => CropSpecResolver.Resolve(specs[index++ % specs.Length], 4000, 3000, album: false));
    }

    [Fact]
    public void ImageHeaderReadingStaysCheap()
    {
        byte[][] headers =
        [
            ImageDimensionReaderTests.Png(1920, 1080),
            ImageDimensionReaderTests.Gif(640, 480),
            ImageDimensionReaderTests.Jpeg(3000, 2000),
            ImageDimensionReaderTests.WebpLossy(800, 600),
        ];
        int index = 0;

        Measure("ImageDimensionReader.TryRead", 200_000, ceilingMicros: 5,
            () => ImageDimensionReader.TryRead(headers[index++ % headers.Length], out _, out _));
    }

    private static (JsonElement, OpEntry) Action(OpSchema schema, string json)
    {
        JsonElement element = JsonDocument.Parse(json).RootElement;
        return (element, schema.Ops[element.GetProperty("op").GetString()!]);
    }
}
