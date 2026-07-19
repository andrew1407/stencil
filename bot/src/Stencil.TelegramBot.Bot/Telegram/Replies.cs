using System.Text;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Static plain-text builders for the bot's chat replies. Kept free of Telegram types so the
/// wording is unit-testable. Mirrors the help/status surface every other front-end exposes
/// (the browser console help registry, the CLI <c>--help</c>, pystencil's prompts).
/// </summary>
public static class Replies
{
    /// <summary>The full slash-command reference, noting that the inline buttons mirror them.</summary>
    public static string HelpText()
    {
        StringBuilder sb = new();
        sb.AppendLine("Stencil bot — edit images with the same core as the browser/desktop/CLI.");
        sb.AppendLine();
        sb.AppendLine("Set the working image by sending a photo, an image file, or just an image link.");
        sb.AppendLine("Add a caption command to apply it at once, e.g. a photo captioned /crop … or /filter bw.");
        sb.AppendLine("Send a video to grab a frame (caption /frame n to pick one); or a .json file with caption /apply.");
        sb.AppendLine();
        sb.AppendLine("Image commands:");
        sb.AppendLine("/blank [format] [w h] [color] — start a blank canvas, e.g. /blank b5 pink");
        sb.AppendLine("/format [name | custom w h] — page format for /blank and the saved layout (bare = list)");
        sb.AppendLine("/url <link> — load an http(s) image");
        sb.AppendLine("/sourcesite <link> [count] [filter=…] [format=…] — scrape a page's media into the chat");
        sb.AppendLine("/sourceupload <link> [index=0] [format=…] — scrape a page and load one image to edit");
        sb.AppendLine("/frame [n] — grab frame n of the loaded video (needs ffmpeg)");
        sb.AppendLine("/crop <spec> [album] — crop, e.g. x1=10% x2=90% y1=10% y2=90%");
        sb.AppendLine("/rotate <n> — rotate n quarter-turns clockwise, e.g. /rotate -1");
        sb.AppendLine("/filter <bw|sepia|invert|contour|none|color> — recolour / tint (a colour = duotone)");
        sb.AppendLine("/undo · /redo — step back / forward one edit   /reset — clear all edits");
        sb.AppendLine("/drop — discard the working image");
        sb.AppendLine("/image — re-render and re-send the current result image");
        sb.AppendLine("/layout <json | link> — apply a layout JSON (or upload the .json file)");
        sb.AppendLine("/json — download the layout JSON");
        sb.AppendLine("/project — download the whole project as a .stencil file (send one back to open it)");
        sb.AppendLine("/status — show the working image, edits and connections");
        sb.AppendLine();
        sb.AppendLine("Drawing (annotate the image — coords are pixels or x%,y%):");
        sb.AppendLine("/draw line x1,y1 x2,y2 … — a polyline");
        sb.AppendLine("/draw rect x1,y1 x2,y2 — a rectangle (two corners)");
        sb.AppendLine("/draw poly x1,y1 x2,y2 x3,y3 … — a closed polygon");
        sb.AppendLine("/color <#hex|name>   /thickness <n>   /markers <n>");
        sb.AppendLine("/style <solid|dashed|dotted>   /fill <#hex|name|none>");
        sb.AppendLine("/pen — show the current pen   /undoline   /clearlines");
        sb.AppendLine();
        sb.AppendLine("Server commands:");
        sb.AppendLine("/connect <url> [token] — connect to a collaboration server");
        sb.AppendLine("/disconnect [url] — forget a connection");
        sb.AppendLine("/connections — list connected servers");
        sb.AppendLine("/projects [url] — list projects (as buttons)");
        sb.AppendLine("/fetch <name|id> — load a project as the working image");
        sb.AppendLine("/create [name] — save the result as a new project");
        sb.AppendLine("/save — save back to the active project");
        sb.AppendLine("/sync [on|off] — live mode: auto-upload edits + pull peers' changes");
        sb.AppendLine("/project-name <text> — rename the working image (or the active project)");
        sb.AppendLine("/project-color <#hex|name|clear> — set the project's accent colour");
        sb.AppendLine("/project-description <text> — set the description (empty clears; saved on /create)");
        sb.AppendLine("/expire <n unit | never> — set the project's expiry, e.g. /expire 3 days");
        sb.AppendLine("/delete — remove the active project from the server (asks to confirm)");
        sb.AppendLine();
        sb.AppendLine("/help — this message   /cancel — never mind");
        sb.Append("The inline buttons mirror these commands.");
        return sb.ToString();
    }

    /// <summary>A short status block: working image label/size, pending edits, active project.</summary>
    public static string StatusText(UserSession session)
    {
        StringBuilder sb = new();
        if (session.HasImage)
        {
            ImageSize size = new(session.OriginalWidth, session.OriginalHeight);
            string label = session.ImageLabel ?? "image";
            sb.AppendLine($"Working image: {label} ({size})");
            if (session.SourceUrl is string src)
            {
                sb.AppendLine($"  source: {src}");
            }
            // The description belongs to the working image whether or not it's saved to a server yet
            // (set via /project-description; carried into /create), so show it here in both cases.
            if (!string.IsNullOrEmpty(session.ActiveProjectDescription))
            {
                sb.AppendLine($"  description: {session.ActiveProjectDescription}");
            }
        }
        else
        {
            sb.AppendLine("Working image: none — send a photo or use /blank.");
        }
        sb.AppendLine($"Pending edits: {DescribeEdits(session.Edits)}");
        if (session.ActiveProjectId is not null)
        {
            string name = session.ActiveProjectName ?? session.ActiveProjectId;
            sb.AppendLine($"Active project: {name} @ {session.ActiveServerUrl} (v{session.ActiveProjectVersion})");
            string created = FmtDate(session.ActiveProjectCreatedAt);
            if (created.Length != 0)
            {
                sb.AppendLine($"  created {created}");
            }
            string expires = FmtDate(session.ActiveProjectExpiresAt);
            if (expires.Length != 0)
            {
                sb.AppendLine($"  expires {expires}");
            }
        }
        else
        {
            sb.AppendLine("Active project: none");
        }
        if (session.VideoSourcePath is not null)
        {
            sb.AppendLine("Video loaded — use /frame n to pick a frame.");
        }
        sb.AppendLine($"Pen: {PenSummary(session.Edits.Pen)}");
        sb.Append($"Connections: {session.Connections.Count}");
        return sb.ToString();
    }

    /// <summary>Usage hint for adding a working image from a link or web page (the Sources button).</summary>
    public static string SourcesHelp()
    {
        StringBuilder sb = new();
        sb.AppendLine("Load a working image from the web — or just send a photo, or paste an image link:");
        sb.AppendLine("/url <link> — load a single image from a direct link");
        sb.AppendLine("/sourcesite <link> [count] [filter=img|video|…] [format=…] [name=<regex>] [group=N] — scrape a page's media into the chat");
        sb.AppendLine("/sourceupload <link> [index] [format=jpg|png|…] [name=<regex>] — scrape a page and load one image to edit");
        sb.Append("e.g. /sourceupload https://en.wikipedia.org/wiki/Cat 0 format=jpg");
        return sb.ToString();
    }

    /// <summary>Usage hint for the <c>/draw</c> family.</summary>
    public static string DrawHelp()
    {
        StringBuilder sb = new();
        sb.AppendLine("Draw onto the image (coordinates are pixels, or x%,y% of the image):");
        sb.AppendLine("/draw line x1,y1 x2,y2 …   — a polyline (2+ points)");
        sb.AppendLine("/draw rect x1,y1 x2,y2     — a rectangle (two opposite corners)");
        sb.AppendLine("/draw poly x1,y1 x2,y2 x3,y3 … — a closed polygon (3+ points)");
        sb.AppendLine();
        sb.Append("Style first with /color /thickness /markers /style /fill — see /pen.");
        return sb.ToString();
    }

    /// <summary>The filter variants for a bare <c>/filter</c> (mirrors the CLI console's list).</summary>
    public static string FilterVariants() =>
        """
        Filter the image: /filter <mode>
        bw — black & white
        sepia — sepia tone
        invert — invert the colours
        contour — edge-detect outline
        none — clear the filter
        …or a colour name/#hex for a duotone tint, e.g. /filter #ff5623
        """;

    /// <summary>The quarter-turn variants for a bare <c>/rotate</c>.</summary>
    public static string RotateVariants() =>
        """
        Rotate by quarter-turns: /rotate <n>
        /rotate 1 — 90° clockwise
        /rotate 2 — 180°
        /rotate -1 — 90° counter-clockwise
        """;

    /// <summary>The crop-spec vocabulary for a bare <c>/crop</c> (and the Crop… button).</summary>
    public static string CropUsage() =>
        """
        Crop: /crop <spec> [album]
        Edges: x1= x2= y1= y2= — each a % of the image, px, or cm.
        e.g. /crop x1=10% x2=90% y1=10% y2=90%
        Add 'album' to derive a missing axis from the page proportion (landscape).
        """;

    /// <summary>Usage hint for <c>/connect</c> (a bare command and the Connect… button).</summary>
    public static string ConnectUsage() =>
        "Use /connect <url> [token] to connect to a server, e.g. /connect http://localhost:8090";

    /// <summary>
    /// The bare <c>/expire</c> / Expiration-button header: the active project's current expiry
    /// (or "no expiry") plus a "choose one" line above the duration picker.
    /// </summary>
    public static string ExpiryPrompt(long expiresAtMs)
    {
        string current = expiresAtMs > 0
            ? $"Current expiry: {FmtDate(expiresAtMs)}."
            : "This project has no expiry (kept forever).";
        return $"{current}\nChoose a new expiry:";
    }

    /// <summary>Usage hint for <c>/expire</c> (an unparseable duration argument).</summary>
    public static string ExpireUsage() =>
        """
        Set the active project's expiry: /expire <amount>
        e.g. /expire 3 days · /expire 1 week · /expire 2 weeks · /expire 1 month · /expire 3 months
        Units: day(s), week(s), fortnight, month(s) — the number may lead or follow (e.g. "week 4").
        /expire never — keep the project forever.
        """;

    /// <summary>The delete-project confirmation question (bare <c>/delete</c> and the 🗑 Remove button).</summary>
    public static string DeleteConfirmPrompt(string name, string? serverUrl)
    {
        string where = serverUrl is null ? "" : $" from {Host(serverUrl)}";
        return $"Delete '{name}'{where}? This permanently removes the project for everyone and can't be undone.";
    }

    /// <summary>
    /// All named page formats with their portrait cm sizes (canonical order), plus the custom
    /// variant — the bare <c>/format</c> reply.
    /// </summary>
    public static string PageFormatList()
    {
        StringBuilder sb = new();
        sb.AppendLine("Page formats (portrait, cm) — set one with /format <name>:");
        foreach (var (name, w, h) in PageFormats.All)
        {
            sb.AppendLine($"{name} ({PageFormats.Cm(w)}×{PageFormats.Cm(h)} cm)");
        }
        sb.AppendLine();
        sb.AppendLine("Custom dims: /format custom <w> <h> (cm), e.g. /format custom 10 15.");
        sb.Append("The chosen format is the /blank default page and rides the saved project layout.");
        return sb.ToString();
    }

    /// <summary>A full description of the current pen.</summary>
    public static string PenText(LineStyle pen)
    {
        StringBuilder sb = new();
        sb.AppendLine("Current pen (applied to new lines):");
        sb.AppendLine($"• colour: {pen.Color}");
        sb.AppendLine($"• thickness: {pen.Thickness}");
        sb.AppendLine($"• markers: {pen.MarkerSize}");
        sb.AppendLine($"• style: {pen.Style}");
        sb.Append($"• fill (closed shapes): {pen.FillColor}");
        return sb.ToString();
    }

    /// <summary>A one-line pen summary for the status block.</summary>
    private static string PenSummary(LineStyle pen) =>
        $"{pen.Color}, {pen.Thickness}px, {pen.Style}, markers {pen.MarkerSize}, fill {pen.FillColor}";

    /// <summary>One-line human summary of the pending <see cref="EditState"/>.</summary>
    public static string DescribeEdits(EditState edits)
    {
        if (edits.IsEmpty)
        {
            return "none";
        }
        List<string> parts = new();
        if (edits.CropSpec is not null)
        {
            parts.Add(edits.Album ? $"crop[{edits.CropSpec}] album" : $"crop[{edits.CropSpec}]");
        }
        if (edits.Rotate != 0)
        {
            parts.Add($"rotate {edits.Rotate * 90}°");
        }
        if (edits.Filter is not null)
        {
            parts.Add($"filter {edits.Filter}");
        }
        if (edits.PageFormat is not null)
        {
            parts.Add(edits.PageFormat == "custom" && edits.CustomPageWidth is double pw && edits.CustomPageHeight is double ph
                ? $"page custom {PageFormats.Cm(pw)}×{PageFormats.Cm(ph)}cm"
                : $"page {edits.PageFormat}");
        }
        if (edits.Layout is not null)
        {
            int lines = edits.Layout.Lines.Count;
            parts.Add($"layout ({lines} line{(lines == 1 ? "" : "s")})");
        }
        return string.Join(", ", parts);
    }

    /// <summary>List the remembered connections (or a hint to /connect when none).</summary>
    public static string ConnectionsText(IReadOnlyList<ServerConnectionInfo> connections)
    {
        if (connections.Count == 0)
        {
            return "No connections. Use /connect <url> [token] to add one.";
        }
        StringBuilder sb = new();
        sb.AppendLine($"Connections ({connections.Count}):");
        for (int i = 0; i < connections.Count; i++)
        {
            ServerConnectionInfo c = connections[i];
            string tls = c.VerifyTls ? "" : " (TLS verification off)";
            sb.AppendLine($"{i + 1}. {c.Url}{tls}");
        }
        return sb.ToString().TrimEnd();
    }

    /// <summary>
    /// The most projects the bot renders in one list message / keyboard. Telegram caps a message
    /// at 4096 chars and an inline keyboard at ~100 buttons, so a server with many projects would
    /// otherwise overflow (a 400 "message is too long"). The overflow is called out, not silently
    /// dropped — narrow with <c>/projects &lt;url&gt;</c> or open directly with <c>/fetch</c>.
    /// </summary>
    public const int MaxProjectsListed = 20;

    /// <summary>List the aggregated cross-server projects (or a hint when none), capped.</summary>
    public static string ProjectsText(IReadOnlyList<ServerProjectInfo> projects)
    {
        if (projects.Count == 0)
        {
            return "No projects found. Connect to a server with /connect first.";
        }
        int shown = Math.Min(projects.Count, MaxProjectsListed);
        StringBuilder sb = new();
        sb.AppendLine($"Projects ({projects.Count}) — tap one to load it:");
        for (int i = 0; i < shown; i++)
        {
            ServerProjectInfo p = projects[i];
            string size = p.Record.HasImage ? $" {p.Record.ImageW}x{p.Record.ImageH}" : "";
            string dot = ColorDot(p.Record.Color);
            string prefix = dot.Length == 0 ? "•" : dot;
            string created = FmtDate(p.Record.CreatedAt);
            string createdBit = created.Length == 0 ? "" : $" · created {created}";
            string expires = FmtDate(p.Record.ExpiresAt);
            string expiresBit = expires.Length == 0 ? "" : $" · expires {expires}";
            sb.AppendLine($"{prefix} {p.Record.Name}{size}{createdBit}{expiresBit} @ {Host(p.ServerUrl)}");
            if (!string.IsNullOrEmpty(p.Record.Description))
            {
                sb.AppendLine($"    {p.Record.Description}");
            }
        }
        if (projects.Count > shown)
        {
            sb.AppendLine($"…and {projects.Count - shown} more — narrow with /projects <url> or open one with /fetch <name|id>.");
        }
        return sb.ToString().TrimEnd();
    }

    /// <summary>
    /// A coloured-circle emoji approximating a project's accent colour (Telegram can't tint text).
    /// A <c>#rgb</c>/<c>#rrggbb</c> maps to the nearest palette dot; a CSS name falls back to 🎨;
    /// empty/unset yields "".
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

    /// <summary>The host[:port] of a normalised origin, for compact labels.</summary>
    public static string Host(string url)
    {
        if (Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            return uri.Authority;
        }
        return url;
    }

    /// <summary>Format an epoch-ms timestamp as an ISO date (UTC), or "" when unset.</summary>
    public static string FmtDate(long ms) =>
        ms <= 0 ? "" : DateTimeOffset.FromUnixTimeMilliseconds(ms).ToString("yyyy-MM-dd");
}
