namespace Stencil.TelegramBot.Application.Llm;

internal sealed record SchemaPath(string Root, string Key, string? Container)
{
    public static string Where(SchemaPath p) =>
        p.Key.Length > 0 ? $"{(p.Container is null ? "" : p.Container + ".")}{p.Root}{p.Key}" : p.Root.TrimEnd('.');

    public static string Label(SchemaPath p) =>
        $"\"{p.Root}{p.Key}\"" + (p.Container is null ? "" : $" in {p.Container}");

    public static SchemaPath Child(SchemaPath? p, string key) =>
        p is not null && p.Key.Length > 0 ? new("", key, Where(p)) : new(p?.Root ?? "", key, null);

    public static SchemaPath Item(SchemaPath p, int i) => new(p.Root, $"{p.Key}[{i}]", p.Container);
}

internal sealed class SchemaError : Exception
{
    public SchemaError(string why) : base(why) { }

    public static Exception Bad(string why) => new SchemaError(why);
}
