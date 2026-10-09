namespace Stencil.TelegramBot.Domain.Layout;

// `stencil --merge-lines`: core's union of a peer's lines with the local ones, the peer's first.
// PeerAdded is false when the peer's lines added nothing the local set lacked.
public sealed record LineMerge(IReadOnlyList<LayoutLine> Lines, bool PeerAdded);
