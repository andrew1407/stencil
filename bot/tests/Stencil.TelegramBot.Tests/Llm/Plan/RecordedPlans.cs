using Stencil.TelegramBot.Application.Llm.Plan;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>A reply judged by core's recorded verdict (<see cref="PlanCheckRecordings"/>) and mapped by the bot — what <see cref="OpPlanParser.ParseAsync"/> yields, in the synchronous shape the plan suites assert on.</summary>
internal static class RecordedPlans
{
    public static OpPlanParseResult Parse(string reply) =>
        OpPlanParser.Map(PlanCheckRecordings.CheckAsync(reply, CancellationToken.None).GetAwaiter().GetResult().Result);
}
