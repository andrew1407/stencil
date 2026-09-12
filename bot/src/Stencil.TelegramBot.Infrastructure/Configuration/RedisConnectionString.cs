using System.Globalization;
using StackExchange.Redis;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

// Accepts the redis://[user:password@]host[:port][/db] URL that .env.example documents and
// StackExchange's own host:port,option=value string; only the second is native, so the URL is
// translated here. Parts are set on the options object, not spliced into a string, so a password
// holding , or = survives; userinfo is percent-decoded.
public static class RedisConnectionString
{
    public const int DefaultPort = 6379;

    private const string PlainScheme = "redis://";
    private const string TlsScheme = "rediss://";

    // The ArgumentException message never quotes the value — it may carry a password.
    public static ConfigurationOptions Parse(string value)
    {
        string raw = value.Trim();
        bool tls = raw.StartsWith(TlsScheme, StringComparison.OrdinalIgnoreCase);
        if (!tls && !raw.StartsWith(PlainScheme, StringComparison.OrdinalIgnoreCase))
        {
            return native(raw);
        }
        if (!Uri.TryCreate(raw, UriKind.Absolute, out Uri? uri) || uri.Host.Length == 0)
        {
            throw invalid();
        }
        ConfigurationOptions options = new() { Ssl = tls };
        options.EndPoints.Add(uri.Host, uri.Port > 0 ? uri.Port : DefaultPort);
        applyUserInfo(options, uri.UserInfo);
        // The path is the database index ("/0"); an empty or non-numeric one leaves the default.
        string db = uri.AbsolutePath.Trim('/');
        if (db.Length > 0 && int.TryParse(db, NumberStyles.None, CultureInfo.InvariantCulture, out int index))
        {
            options.DefaultDatabase = index;
        }
        return options;
    }

    private static ConfigurationOptions native(string raw)
    {
        try
        {
            return ConfigurationOptions.Parse(raw);
        }
        catch (Exception ex) when (ex is ArgumentException or FormatException)
        {
            throw invalid();
        }
    }

    private static void applyUserInfo(ConfigurationOptions options, string userInfo)
    {
        if (userInfo.Length == 0)
        {
            return;
        }
        int split = userInfo.IndexOf(':');
        string user = Uri.UnescapeDataString(split < 0 ? userInfo : userInfo[..split]);
        string password = split < 0 ? "" : Uri.UnescapeDataString(userInfo[(split + 1)..]);
        if (user.Length > 0)
        {
            options.User = user;
        }
        if (password.Length > 0)
        {
            options.Password = password;
        }
    }

    private static ArgumentException invalid() => new(
        "REDIS_URL must be a redis://[user:password@]host[:port][/db] URL or a "
        + "host:port[,option=value] configuration string");
}
