// POSIX regex shim for the CLI's --source-name filter. Zig 0.16's translate-c renders
// glibc's `regex_t` as an opaque type (it carries bitfields), so it can no longer be embedded
// by value in a Zig struct. This shim owns the `regex_t` storage on the C side — where
// `sizeof(regex_t)` is known natively, so no size guessing and it stays portable across libcs
// — and hands Zig an opaque handle. Compiled only where <regex.h> exists (POSIX); on
// Windows/WASI the entry points compile away and scrape.zig uses its substring fallback.
#if !defined(_WIN32) && !defined(__wasi__)
#include <regex.h>
#include <stdlib.h>

// Compile PATTERN as a case-insensitive POSIX extended regex (REG_NOSUB — match/no-match
// only, no capture groups). Returns an opaque handle, or NULL on an invalid pattern or OOM.
void *stencil_regex_compile(const char *pattern) {
    regex_t *re = (regex_t *)malloc(sizeof(regex_t));
    if (re == NULL) return NULL;
    if (regcomp(re, pattern, REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0) {
        free(re);
        return NULL;
    }
    return re;
}

// Does the compiled regex match anywhere in TEXT? 1 = match, 0 = no match (or NULL handle).
int stencil_regex_match(void *handle, const char *text) {
    if (handle == NULL) return 0;
    return regexec((regex_t *)handle, text, 0, NULL, 0) == 0 ? 1 : 0;
}

// Free a handle returned by stencil_regex_compile (NULL-safe).
void stencil_regex_free(void *handle) {
    if (handle != NULL) {
        regfree((regex_t *)handle);
        free(handle);
    }
}
#endif
