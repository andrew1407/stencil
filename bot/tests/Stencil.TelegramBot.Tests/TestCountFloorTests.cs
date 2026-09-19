using System.Reflection;
using Xunit.Sdk;

namespace Stencil.TelegramBot.Tests;

/// <summary>A floor under the number of xunit cases this assembly runs: a suite can report no failures while executing a fraction of itself (a dropped project reference, a filter typo), and pass/fail cannot see that. Counted by reflection, the way the runner sees it.</summary>
public sealed class TestCountFloorTests
{
    // A floor, not a pin: raise it when the suite grows a lot. Additions never trip it.
    private const int _testCaseFloor = 2080;

    [Fact]
    public void Should_Run_At_Least_The_Floor_Of_Test_Cases()
    {
        int found = typeof(TestCountFloorTests).Assembly.GetTypes()
            .Where(isRunnableClass)
            .SelectMany(type => type.GetMethods(BindingFlags.Public | BindingFlags.Instance))
            .Where(isRunnableTest)
            .Sum(caseCount);
        Assert.True(found >= _testCaseFloor,
            $"bot suite collapsed to {found} test cases, floor is {_testCaseFloor}");
    }

    private static bool isRunnableClass(Type type) =>
        type is { IsClass: true, IsAbstract: false, ContainsGenericParameters: false };

    private static bool isRunnableTest(MethodInfo method)
    {
        FactAttribute? fact = method.GetCustomAttribute<FactAttribute>();
        return fact is { Skip: null } && !isBench(method) && !isBench(method.DeclaringType!);
    }

    /// <summary>The csproj filters <c>Category!=Bench</c> out of a plain run, so this must too.</summary>
    private static bool isBench(MemberInfo member) =>
        member.GetCustomAttributesData().Any(data =>
            data.AttributeType == typeof(TraitAttribute)
            && data.ConstructorArguments.Count == 2
            && (string?)data.ConstructorArguments[0].Value == "Category"
            && (string?)data.ConstructorArguments[1].Value == "Bench");

    /// <summary>A [Theory] runs once per data row; a [Fact] once.</summary>
    private static int caseCount(MethodInfo method)
    {
        DataAttribute[] rows = [.. method.GetCustomAttributes<DataAttribute>()];
        return rows.Length == 0 ? 1 : rows.Sum(row => row.GetData(method).Count());
    }
}
