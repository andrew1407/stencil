using System.Globalization;
using System.Text;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Bot.Telegram;

// Replies — the server tables: remembered connections, the project list, and the colour dot and
// host/date helpers they render with. Class doc lives in Replies.cs.
public static partial class Replies
{
    public static string ConnectionsUsage() => BotStrings.Reply("connectionsUsage");

    /// <summary>
    /// The remembered connections (or a hint when none), narrowed by an <c>admin</c>/<c>session</c>
    /// <paramref name="filter"/>. Admin connections are marked — never the token itself.
    /// </summary>
    public static string ConnectionsText(IReadOnlyList<ServerConnectionInfo> connections, string filter = "")
    {
        if (connections.Count == 0)
        {
            return filter.Length == 0
                ? BotStrings.Reply("connectionsEmpty")
                : BotStrings.Reply("connectionsEmptyFiltered", filter);
        }
        string header = filter switch
        {
            "admin" => BotStrings.Reply("connectionsHeaderAdmin", connections.Count),
            "session" => BotStrings.Reply("connectionsHeaderSession", connections.Count),
            _ => BotStrings.Reply("connectionsHeader", connections.Count),
        };
        StringBuilder sb = new();
        sb.AppendLine(header);
        for (int i = 0; i < connections.Count; i++)
        {
            ServerConnectionInfo c = connections[i];
            string tls = c.VerifyTls ? "" : BotStrings.Reply("connectionsTlsOff");
            string kind = c.CredentialKind == CredentialKind.Admin ? BotStrings.Reply("connectionsAdminMark") : "";
            sb.AppendLine(BotStrings.Reply("connectionsEntry", i + 1, c.Url, kind, tls));
        }
        return sb.ToString().TrimEnd();
    }

    /// <summary>
    /// The most projects one list message / keyboard renders — Telegram caps both. The overflow
    /// is called out, never silently dropped (narrow with <c>/projects &lt;url&gt;</c>).
    /// </summary>
    public const int MaxProjectsListed = 20;

    public static string ProjectsText(IReadOnlyList<ServerProjectInfo> projects)
    {
        if (projects.Count == 0)
        {
            return BotStrings.Reply("projectsEmpty");
        }
        int shown = Math.Min(projects.Count, MaxProjectsListed);
        StringBuilder sb = new();
        sb.AppendLine(BotStrings.Reply("projectsHeader", projects.Count));
        for (int i = 0; i < shown; i++)
        {
            ServerProjectInfo p = projects[i];
            string size = p.Record.HasImage ? BotStrings.Reply("projectsSize", p.Record.ImageW, p.Record.ImageH) : "";
            string dot = ColorDot(p.Record.Color);
            string prefix = dot.Length == 0 ? "•" : dot;
            string created = FmtDate(p.Record.CreatedAt);
            string createdBit = created.Length == 0 ? "" : BotStrings.Reply("projectsCreated", created);
            string expires = FmtDate(p.Record.ExpiresAt);
            string expiresBit = expires.Length == 0 ? "" : BotStrings.Reply("projectsExpires", expires);
            sb.AppendLine(BotStrings.Reply("projectsEntry", prefix, p.Record.Name, size, createdBit, expiresBit, Host(p.ServerUrl)));
            if (!string.IsNullOrEmpty(p.Record.Description))
            {
                sb.AppendLine(BotStrings.Reply("projectsDescription", p.Record.Description));
            }
        }
        if (projects.Count > shown)
        {
            sb.AppendLine(BotStrings.Reply("projectsOverflow", projects.Count - shown));
        }
        return sb.ToString().TrimEnd();
    }

    /// <summary>
    /// A coloured-circle emoji for a project's accent (Telegram can't tint text): a hex maps to
    /// the nearest palette dot, a CSS name falls back to 🎨, empty yields "".
    /// </summary>
    public static string ColorDot(string? color)
    {
        if (string.IsNullOrWhiteSpace(color))
        {
            return "";
        }
        if (!TryParseHex(color, out int r, out int g, out int b))
        {
            return "🎨"; // a named colour we can't cheaply resolve — still signals "has a colour"
        }
        (int R, int G, int B, string Dot)[] palette =
        [
            (220, 50, 50, "🔴"), (240, 150, 30, "🟠"), (245, 220, 60, "🟡"),
            (60, 180, 75, "🟢"), (60, 120, 220, "🔵"), (150, 80, 200, "🟣"),
            (140, 90, 60, "🟤"), (30, 30, 30, "⚫"), (240, 240, 240, "⚪"),
        ];
        string best = "🎨";
        long bestDist = long.MaxValue;
        foreach (var c in palette)
        {
            long d = (long)(c.R - r) * (c.R - r) + (long)(c.G - g) * (c.G - g) + (long)(c.B - b) * (c.B - b);
            if (d < bestDist)
            {
                bestDist = d;
                best = c.Dot;
            }
        }
        return best;
    }

    private static bool TryParseHex(string color, out int r, out int g, out int b)
    {
        r = g = b = 0;
        string s = color.Trim().TrimStart('#');
        if (s.Length == 3)
        {
            s = string.Concat(s[0], s[0], s[1], s[1], s[2], s[2]);
        }
        if (s.Length != 6)
        {
            return false;
        }
        return int.TryParse(s.AsSpan(0, 2), System.Globalization.NumberStyles.HexNumber, null, out r)
            && int.TryParse(s.AsSpan(2, 2), System.Globalization.NumberStyles.HexNumber, null, out g)
            && int.TryParse(s.AsSpan(4, 2), System.Globalization.NumberStyles.HexNumber, null, out b);
    }

    public static string Host(string url)
    {
        if (Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            return uri.Authority;
        }
        return url;
    }

    public static string FmtDate(long ms) =>
        ms <= 0 ? "" : DateTimeOffset.FromUnixTimeMilliseconds(ms).ToString("yyyy-MM-dd");
}
