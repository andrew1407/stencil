namespace Stencil.TelegramBot.Domain.Llm;

// `stencil --script-plan` (cli/CONTRACT.md §5): one .stc lowered to op-plan JSON. The envelope's
// own label is replaced by LABEL on the way in, so no workspace path can reach a reply.
public sealed record ScriptPlan(
    IReadOnlyList<ScriptDiagnostic> Diagnostics,
    IReadOnlyList<ScriptBlock> Blocks)
{
    public const string LABEL = "script.stc";

    public bool HasErrors => Diagnostics.Any(static d => d.IsError);

    public IEnumerable<ScriptDiagnostic> Errors => Diagnostics.Where(static d => d.IsError);

    public IEnumerable<ScriptDiagnostic> Warnings => Diagnostics.Where(static d => !d.IsError);
}

public sealed record ScriptDiagnostic(string Severity, string Code, int Line, int Col, string Message)
{
    public const string SEVERITY_ERROR = "error";

    public bool IsError => string.Equals(Severity, SEVERITY_ERROR, StringComparison.Ordinal);

    // The same one-line grammar `--script-check` prints and the editors parse.
    public override string ToString() => $"{ScriptPlan.LABEL}:{Line}:{Col}: {Severity}: {Message} [{Code}]";
}

// One `@source` block, or the sourceless one that edits the working image. Each Plans entry is one
// chunk's `actions` array as raw JSON, already capped at the envelope's MAX_ACTIONS.
public sealed record ScriptBlock(
    int Index,
    string Source,
    string SourceKind,
    IReadOnlyList<string> Inputs,
    IReadOnlyList<string> Plans)
{
    public const string KIND_PROJECT = "project";
    public const string KIND_URL = "url";

    // file / dir / glob name paths on the machine that ran the CLI, which a chat user never has.
    public bool IsLocalSource => SourceKind is not (KIND_PROJECT or KIND_URL);
}
