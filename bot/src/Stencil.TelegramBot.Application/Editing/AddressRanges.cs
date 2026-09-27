using System.Net;
using System.Text.Json;

namespace Stencil.TelegramBot.Application.Editing;

// The fetch guard's address table, parsed once from the embedded browser/js/config/net/blockedRanges.json
// (semantics in its blockedRanges.README.md). An IPv6 form that carries an IPv4 address is judged by it.
public static class AddressRanges
{
    public const string FETCH = "fetch";
    public const string SERVER_TARGET = "serverTarget";
    public const string ALLOW_LOOPBACK = "allowLoopback";
    public const string ALLOW_PRIVATE = "allowPrivate";

    private const string _resourceName = "Stencil.TelegramBot.Application.Assets.blockedRanges.json";

    // A prefix matched on the raw bytes: IPNetwork.Contains misses an IPv4-mapped address under ::ffff:0:0/96.
    private sealed record Cidr(byte[] Bytes, int Prefix)
    {
        public static Cidr Parse(string text)
        {
            IPNetwork network = IPNetwork.Parse(text);
            return new Cidr(network.BaseAddress.GetAddressBytes(), network.PrefixLength);
        }

        public bool Contains(IPAddress address)
        {
            byte[] b = address.GetAddressBytes();
            if (b.Length != Bytes.Length)
            {
                return false;
            }
            for (int i = 0, left = Prefix; left > 0; i++, left -= 8)
            {
                int mask = left >= 8 ? 0xFF : (0xFF << (8 - left)) & 0xFF;
                if ((b[i] & mask) != (Bytes[i] & mask))
                {
                    return false;
                }
            }
            return true;
        }
    }

    private sealed record Embed(Cidr Prefix, int Offset, Cidr[] Except);

    private sealed record Policy(string[] Blocks, Dictionary<string, string[]> BlocksUnless);

    private sealed record Table(
        Dictionary<string, Cidr[]> Classes, Embed[] Embeds, Dictionary<string, Policy> Policies);

    private static readonly Lazy<Table> _table = new(load);

    // True when `policy` refuses the address; `options` are the policy's blocksUnless switches it passes.
    public static bool Blocks(string policy, IPAddress address, params string[] options)
    {
        Table table = _table.Value;
        Policy p = table.Policies.TryGetValue(policy, out Policy? found)
            ? found
            : throw new ArgumentException($"blockedRanges.json has no policy \"{policy}\"", nameof(policy));
        IEnumerable<string> classes = p.Blocks.Concat(p.BlocksUnless
            .Where(unless => !options.Contains(unless.Key))
            .SelectMany(unless => unless.Value));
        IPAddress judged = Carried(address);
        return classes.Any(name => table.Classes[name].Any(network => network.Contains(judged)));
    }

    // The IPv4 address an embedsV4 prefix (mapped, compatible, NAT64, 6to4) carries, else the address.
    public static IPAddress Carried(IPAddress address)
    {
        Embed? embed = _table.Value.Embeds.FirstOrDefault(e =>
            e.Prefix.Contains(address) && !e.Except.Any(x => x.Contains(address)));
        return embed is null ? address : new IPAddress(address.GetAddressBytes().AsSpan(embed.Offset, 4));
    }

    private static Table load()
    {
        using Stream stream = typeof(AddressRanges).Assembly.GetManifestResourceStream(_resourceName)
            ?? throw new InvalidOperationException($"embedded resource {_resourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        JsonElement root = doc.RootElement;
        Dictionary<string, Cidr[]> classes = root.GetProperty("classes").EnumerateObject()
            .ToDictionary(c => c.Name, c => networks(c.Value), StringComparer.Ordinal);
        Embed[] embeds = [.. root.GetProperty("embedsV4").EnumerateArray().Select(e => new Embed(
            Cidr.Parse(e.GetProperty("prefix").GetString()!),
            e.GetProperty("offset").GetInt32(),
            e.TryGetProperty("except", out JsonElement except) ? networks(except) : []))];
        Dictionary<string, Policy> policies = root.GetProperty("policies").EnumerateObject()
            .ToDictionary(p => p.Name, p => new Policy(
                names(p.Value.GetProperty("blocks")),
                p.Value.TryGetProperty("blocksUnless", out JsonElement unless)
                    ? unless.EnumerateObject().ToDictionary(u => u.Name, u => names(u.Value), StringComparer.Ordinal)
                    : new Dictionary<string, string[]>()), StringComparer.Ordinal);
        return new Table(classes, embeds, policies);
    }

    private static Cidr[] networks(JsonElement list) =>
        [.. list.EnumerateArray().Select(static cidr => Cidr.Parse(cidr.GetString()!))];

    private static string[] names(JsonElement list) =>
        [.. list.EnumerateArray().Select(static name => name.GetString()!)];
}
