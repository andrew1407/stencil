using System.Text.Json;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Domain.Project;

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

// Mirrors projectFile.js and the CLI's project.zig.
public static class StencilProjectFile
{
    public const string FORMAT = "stencil-project";
    public const int VERSION = 1;

    private static readonly Dictionary<string, string> _mimeByExt = new(StringComparer.Ordinal)
    {
        ["png"] = "image/png", ["jpg"] = "image/jpeg", ["jpeg"] = "image/jpeg",
        ["bmp"] = "image/bmp", ["webp"] = "image/webp", ["gif"] = "image/gif",
    };

    private static string mimeForExt(string ext) =>
        _mimeByExt.GetValueOrDefault(ext.ToLowerInvariant(), "application/octet-stream");

    private static Dictionary<string, object?> buildRoot(StencilProject project)
    {
        var image = new Dictionary<string, object?>
        {
            ["dataUrl"] = $"data:{mimeForExt(project.ImageExt)};base64,{Convert.ToBase64String(project.ImageBytes)}",
            ["ext"] = project.ImageExt,
            ["w"] = project.ImageWidth,
            ["h"] = project.ImageHeight,
        };
        var root = new Dictionary<string, object?>
        {
            ["format"] = FORMAT,
            ["version"] = VERSION,
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

    public static string Build(StencilProject project) =>
        JsonSerializer.Serialize(buildRoot(project), StencilJson.Indented);

    public static byte[] BuildUtf8(StencilProject project) =>
        JsonSerializer.SerializeToUtf8Bytes(buildRoot(project), StencilJson.Indented);

    public static StencilProject? Parse(byte[] bytes)
    {
        try
        {
            using JsonDocument doc = JsonDocument.Parse(bytes);
            JsonElement root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return null;
            if (!root.TryGetProperty("format", out JsonElement fmt) || fmt.GetString() != FORMAT) return null;
            int version = root.TryGetProperty("version", out JsonElement ver) && ver.TryGetInt32(out int v) ? v : 0;
            if (version < 1 || version > VERSION) return null;

            if (!root.TryGetProperty("image", out JsonElement img) || img.ValueKind != JsonValueKind.Object) return null;
            string dataUrl = img.TryGetProperty("dataUrl", out JsonElement du) ? du.GetString() ?? "" : "";
            int idx = dataUrl.IndexOf("base64,", StringComparison.Ordinal);
            if (idx < 0) return null;
            byte[] imageBytes = Convert.FromBase64String(dataUrl[(idx + "base64,".Length)..]);
            if (imageBytes.Length == 0) return null;

            return new StencilProject
            {
                Name = getString(root, "name") ?? "Untitled",
                Color = getString(root, "color"),
                Keywords = getStringList(root, "keywords"),
                Source = getString(root, "source"),
                Resource = getString(root, "resource"),
                Blank = root.TryGetProperty("blank", out JsonElement bl) && bl.ValueKind == JsonValueKind.True,
                BlankColor = getString(root, "blankColor"),
                ImageBytes = imageBytes,
                ImageExt = getString(img, "ext") ?? "png",
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

    private static string? getString(JsonElement obj, string key) =>
        obj.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

    private static IReadOnlyList<string> getStringList(JsonElement obj, string key)
    {
        if (!obj.TryGetProperty(key, out JsonElement arr) || arr.ValueKind != JsonValueKind.Array) return [];
        var list = new List<string>();
        foreach (JsonElement e in arr.EnumerateArray())
            if (e.ValueKind == JsonValueKind.String && e.GetString() is { } s) list.Add(s);
        return list;
    }
}
