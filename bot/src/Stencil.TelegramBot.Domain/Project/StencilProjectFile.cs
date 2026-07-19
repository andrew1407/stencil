using System.Text.Json;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Domain.Project;

/// <summary>One parsed <c>.stencil</c> project: original image bytes, metadata, and the raw export-layout <see cref="JsonElement"/>.</summary>
public sealed record StencilProject
{
    public string Name { get; init; } = "Untitled";
    public string? Color { get; init; }
    public IReadOnlyList<string> Keywords { get; init; } = [];
    public string? Source { get; init; }
    public string? Resource { get; init; }
    public bool Blank { get; init; }
    public string? BlankColor { get; init; }
    public byte[] ImageBytes { get; init; } = [];
    public string ImageExt { get; init; } = "png";
    public int ImageWidth { get; init; }
    public int ImageHeight { get; init; }
    public JsonElement? Layout { get; init; }
}

/// <summary>Build/parse the shared <c>.stencil</c> format (image + layout + metadata JSON); mirrors <c>projectFile.js</c> and the CLI's <c>project.zig</c>.</summary>
public static class StencilProjectFile
{
    public const string Format = "stencil-project";
    public const int Version = 1;

    private static string MimeForExt(string ext) => ext.ToLowerInvariant() switch
    {
        "png" => "image/png",
        "jpg" or "jpeg" => "image/jpeg",
        "bmp" => "image/bmp",
        "webp" => "image/webp",
        "gif" => "image/gif",
        _ => "application/octet-stream",
    };

    /// <summary>Assemble the ordered property bag for a <c>.stencil</c> document (empty metadata omitted).</summary>
    private static Dictionary<string, object?> BuildRoot(StencilProject project)
    {
        var image = new Dictionary<string, object?>
        {
            ["dataUrl"] = $"data:{MimeForExt(project.ImageExt)};base64,{Convert.ToBase64String(project.ImageBytes)}",
            ["ext"] = project.ImageExt,
            ["w"] = project.ImageWidth,
            ["h"] = project.ImageHeight,
        };
        var root = new Dictionary<string, object?>
        {
            ["format"] = Format,
            ["version"] = Version,
            ["name"] = string.IsNullOrEmpty(project.Name) ? "Untitled" : project.Name,
        };
        if (!string.IsNullOrEmpty(project.Color)) root["color"] = project.Color;
        if (project.Keywords.Count > 0) root["keywords"] = project.Keywords;
        if (!string.IsNullOrEmpty(project.Source)) root["source"] = project.Source;
        if (!string.IsNullOrEmpty(project.Resource)) root["resource"] = project.Resource;
        if (project.Blank)
        {
            root["blank"] = true;
            if (!string.IsNullOrEmpty(project.BlankColor)) root["blankColor"] = project.BlankColor;
        }
        root["image"] = image;
        if (project.Layout is { } layout) root["layout"] = layout;
        return root;
    }

    /// <summary>Serialize a project to pretty-printed <c>.stencil</c> JSON.</summary>
    public static string Build(StencilProject project) =>
        JsonSerializer.Serialize(BuildRoot(project), StencilJson.Indented);

    /// <summary>Serialize a project straight to UTF-8 <c>.stencil</c> bytes — avoids the extra
    /// full-document string copy of <see cref="Build"/> on the (image-bearing) export path.</summary>
    public static byte[] BuildUtf8(StencilProject project) =>
        JsonSerializer.SerializeToUtf8Bytes(BuildRoot(project), StencilJson.Indented);

    /// <summary>Parse + validate <c>.stencil</c> bytes; null on malformed / foreign / too-new files.</summary>
    public static StencilProject? Parse(byte[] bytes)
    {
        try
        {
            using JsonDocument doc = JsonDocument.Parse(bytes);
            JsonElement root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return null;
            if (!root.TryGetProperty("format", out JsonElement fmt) || fmt.GetString() != Format) return null;
            int version = root.TryGetProperty("version", out JsonElement ver) && ver.TryGetInt32(out int v) ? v : 0;
            if (version < 1 || version > Version) return null;

            if (!root.TryGetProperty("image", out JsonElement img) || img.ValueKind != JsonValueKind.Object) return null;
            string dataUrl = img.TryGetProperty("dataUrl", out JsonElement du) ? du.GetString() ?? "" : "";
            int marker = dataUrl.IndexOf("base64,", StringComparison.Ordinal);
            if (marker < 0) return null;
            byte[] imageBytes = Convert.FromBase64String(dataUrl[(marker + "base64,".Length)..]);
            if (imageBytes.Length == 0) return null;

            return new StencilProject
            {
                Name = GetString(root, "name") ?? "Untitled",
                Color = GetString(root, "color"),
                Keywords = GetStringList(root, "keywords"),
                Source = GetString(root, "source"),
                Resource = GetString(root, "resource"),
                Blank = root.TryGetProperty("blank", out JsonElement bl) && bl.ValueKind == JsonValueKind.True,
                BlankColor = GetString(root, "blankColor"),
                ImageBytes = imageBytes,
                ImageExt = GetString(img, "ext") ?? "png",
                ImageWidth = img.TryGetProperty("w", out JsonElement w) && w.TryGetInt32(out int wi) ? wi : 0,
                ImageHeight = img.TryGetProperty("h", out JsonElement h) && h.TryGetInt32(out int hi) ? hi : 0,
                Layout = root.TryGetProperty("layout", out JsonElement lay) ? lay.Clone() : null,
            };
        }
        catch (Exception ex) when (ex is JsonException or FormatException)
        {
            return null;
        }
    }

    private static string? GetString(JsonElement obj, string key) =>
        obj.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

    private static IReadOnlyList<string> GetStringList(JsonElement obj, string key)
    {
        if (!obj.TryGetProperty(key, out JsonElement arr) || arr.ValueKind != JsonValueKind.Array) return [];
        var list = new List<string>();
        foreach (JsonElement e in arr.EnumerateArray())
            if (e.ValueKind == JsonValueKind.String && e.GetString() is { } s) list.Add(s);
        return list;
    }
}
