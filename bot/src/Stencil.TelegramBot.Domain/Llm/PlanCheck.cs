namespace Stencil.TelegramBot.Domain.Llm;

// `stencil --plan-check` (cli/CONTRACT.md §7): core's verdict on one model reply. Result is the
// envelope's `result` object as raw JSON; the registry size and FNV-1a 64 expose a skewed CLI.
public sealed record PlanCheck(long RegistryBytes, string RegistryFnv1a64, string Result);
