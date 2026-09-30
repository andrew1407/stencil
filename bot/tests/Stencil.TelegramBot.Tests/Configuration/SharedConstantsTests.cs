using System.Text.Json;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Tests.Configuration;

/// <summary>Defaults the bot spells as literals, pinned to their shared value in <c>common/config/constants.json</c>.</summary>
public sealed class SharedConstantsTests
{
    [Fact]
    public void Should_Default_The_Browser_App_Url_To_The_Shared_App_Url()
    {
        using JsonDocument constants = JsonDocument.Parse(
            File.ReadAllText(SharedFixtures.PathOf("common", "config", "constants.json")));
        string appUrl = constants.RootElement.GetProperty("NETWORK").GetProperty("appUrl").GetString()!;
        Assert.Equal(appUrl.TrimEnd('/'), BotOptions.DEFAULT_BROWSER_APP_URL);
    }
}
