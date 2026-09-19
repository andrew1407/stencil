package protocol

// File kinds accepted by the files endpoints and the filestore. Besides the
// fixed kinds below, the eight LLM variant slots "variant1".."variant8"
// (IsVariantKind) are accepted; video/variant/chat bytes live in the filestore
// only and are removed with the project (llm-contract.md §9). "chat" holds
// the persisted-chat JSON document (§12), uploaded with ext=json.
const (
	KindOriginal = "original"
	KindResult   = "result"
	KindVideo    = "video"
	KindChat     = "chat"
)

// IsVariantKind reports whether kind names one of the eight per-project variant
// slots ("variant1".."variant8") — the same cap as llm-contract.md's op plan.
func IsVariantKind(kind string) bool {
	const prefix = "variant"
	if len(kind) != len(prefix)+1 || kind[:len(prefix)] != prefix {
		return false
	}
	n := kind[len(prefix)]
	return n >= '1' && n <= '8'
}

// IsFileKind reports whether kind is an accepted project file kind — the single
// allowlist shared by the files endpoints and the filestore.
func IsFileKind(kind string) bool {
	return kind == KindOriginal || kind == KindResult ||
		kind == KindVideo || kind == KindChat || IsVariantKind(kind)
}

// IsFilestoreOnlyKind reports whether kind lives in the filestore only (no project-record columns): the
// kinds the per-file DELETE route may remove (llm-contract.md §9). original/result go with the project.
func IsFilestoreOnlyKind(kind string) bool {
	return kind == KindVideo || kind == KindChat || IsVariantKind(kind)
}
