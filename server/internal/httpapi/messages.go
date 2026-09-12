package httpapi

// The client-facing prose, in one place. protocol.Code* is the contract a client
// branches on; these strings are the human half, and scattering them over ten
// handler files made the wording impossible to review as a set. Changing one
// changes what users read — the goldens under goldens/ pin the LLM surface.

// Project and file lookups.
const (
	msgProjectNotFound = "project not found"
	msgLoadProject     = "could not load project"
	msgListProjects    = "could not list projects"
	msgCreateProject   = "could not create project"
	msgUpdateProject   = "could not update project"
	msgDeleteProject   = "could not delete project"
	msgStaleVersion    = "stale version; reload and retry"
)

// The two project rules that are policy, not plumbing (internal/service).
const (
	msgImageRequired = "a project must be created from an image"
	msgProjectInUse  = "project is in use by other clients; cannot delete"
)

// Files.
const (
	msgUnknownFileKind = "unknown file kind"
	msgFileMissing     = "file missing"
	msgReadFile        = "could not read file"
	msgListFiles       = "could not list files"
	msgStoreFile       = "could not store file"
	msgRecordFile      = "could not record file"
	msgDeleteFile      = "could not delete file"
	msgEmptyBody       = "empty file body"
	msgBodyTooLarge    = "body too large or unreadable"
	msgRejectedPath    = "rejected path"
	msgQuotaExceeded   = "server storage quota exceeded"
)

// The two file messages that name the kind they are about.
func msgNoFileOfKind(kind string) string { return "no " + kind + " file" }

func msgKindGoesWithProject(kind string) string { return kind + " is removed with the project" }

// Auth and request framing.
const (
	msgAdminRequired  = "admin token required to issue tokens"
	msgTokenGenFailed = "token generation failed"
	msgPersistSession = "could not persist session"
	msgInvalidJSONPre = "invalid JSON body: " // suffixed by the decoder's reason
)

// Rate limiting. tooManyRequests appends msgRetryLater.
const (
	msgTooManyTokenRequests = "too many token requests"
	msgWriteRateExceeded    = "write rate exceeded"
	msgRetryLater           = "; retry later"
	msgLlmRateLimited       = "too many assistant requests — wait a moment and try again"
	msgLlmBusy              = "the assistant is busy — too many requests in flight"
)

// The LLM proxy.
const (
	msgLlmDisabled = "LLM proxy is not configured on this server"
	msgLlmFailed   = "LLM request failed"
)
