using System.Globalization;
using StackExchange.Redis;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

/// <summary>
/// Turns whatever <c>REDIS_URL</c> holds into StackExchange.Redis options.
/// </summary>
/// <remarks>
/// Two forms are accepted: the <c>redis://[user:password@]host[:port][/db]</c> URL that
/// <c>bot/.env.example</c> documents (and that the Go server, pystencil and every hosted Redis
/// hand out), and StackExchange's own <c>host:port,option=value</c> configuration string.
/// Only the second is native — passing a URL straight to <c>ConnectionMultiplexer.Connect</c>
/// throws at startup, so the documented value used to crash the bot.
/// <para>
/// The URL's parts are set on the options object rather than spliced into a configuration
/// string, so a password containing a <c>,</c> or <c>=</c> survives intact. Percent-escapes in
/// the userinfo are decoded (that is how such a password must be written in a URL).
/// </para>
/// </remarks>
public static class RedisConnectionString
{
    /// <summary>Redis's default port, used when the URL names none.</summary>
    public const int DefaultPort = 6379;

    private const string PlainScheme = "redis://";
    private const string TlsScheme = "rediss://";

    /// <summary>
    /// Parse <paramref name="value"/> into connection options.
    /// </summary>
    /// <exception cref="ArgumentException">
    /// The value is neither a valid <c>redis(s)://</c> URL nor a valid configuration string. The
    /// message never quotes the value — it may carry a password.
    /// </exception>
    public static ConfigurationOptions Parse(string value)
    {
        string raw = value.Trim();
        bool tls = raw.StartsWith(TlsScheme, StringComparison.OrdinalIgnoreCase);
        if (!tls && !raw.StartsWith(PlainScheme, StringComparison.OrdinalIgnoreCase))
        {
            return Native(raw);
        }
        if (!Uri.TryCreate(raw, UriKind.Absolute, out Uri? uri) || uri.Host.Length == 0)
        {
            throw Invalid();
        }
        ConfigurationOptions options = new() { Ssl = tls };
        options.EndPoints.Add(uri.Host, uri.Port > 0 ? uri.Port : DefaultPort);
        ApplyUserInfo(options, uri.UserInfo);
        // The path is the database index ("/0"); an empty or non-numeric one leaves the default.
        string db = uri.AbsolutePath.Trim('/');
        if (db.Length > 0 && int.TryParse(db, NumberStyles.None, CultureInfo.InvariantCulture, out int index))
        {
            options.DefaultDatabase = index;
        }
        return options;
    }

    /// <summary>StackExchange's own syntax, with its parse failure reported like ours.</summary>
    private static ConfigurationOptions Native(string raw)
    {
        try
        {
            return ConfigurationOptions.Parse(raw);
        }
        catch (Exception ex) when (ex is ArgumentException or FormatException)
        {
            throw Invalid();
        }
    }

    /// <summary>Split <c>user:password</c> (either half may be empty) onto the options.</summary>
    private static void ApplyUserInfo(ConfigurationOptions options, string userInfo)
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

    private static ArgumentException Invalid() => new(
        "REDIS_URL must be a redis://[user:password@]host[:port][/db] URL or a "
        + "host:port[,option=value] configuration string");
}
