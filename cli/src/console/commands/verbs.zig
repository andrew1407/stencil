//! The console's session verbs — every command that is not an image transform — and the words
//! that name them; `commands.zig` re-exports both. `tests/console/repl_text_test.zig` holds the
//! help text to this list and `tests/console/ui_test.zig` the Tab-completions.
const std = @import("std");

pub const Verb = enum { browser, desktop, desktopsync, script, script_run, upload, source_upload, blank, save, delete, layout, formula, format, exec, undo, redo, reset, drop, clear, copy, paste, unpaste, images, theme, mouse, reveal_speed, status, help, quit, connect, disconnect, reconnect, connections, projects, project_color, blank_color, project_description, rename, expire, fetch, sync, keywords, keywords_search, keywords_add, keywords_del, prompt, llm, chat };

// Session-level verbs (everything that is not an image transform). Returns null for words
// that name a transform (crop/rotate/flip/filter/apply) or are unknown.
pub fn verbOf(w: []const u8) ?Verb {
    const eq = std.ascii.eqlIgnoreCase;
    if (eq(w, "upload") or eq(w, "open") or eq(w, "load")) return .upload;
    if (eq(w, "source-upload") or eq(w, "sourceupload") or eq(w, "scrape")) return .source_upload;
    if (eq(w, "blank") or eq(w, "new")) return .blank;
    if (eq(w, "save") or eq(w, "write")) return .save;
    // Delete a local .stencil project file from disk (parity with the browser/desktop trash button).
    if (eq(w, "delete") or eq(w, "del") or eq(w, "remove") or eq(w, "rm")) return .delete;
    if (eq(w, "layout") or eq(w, "exportlayout") or eq(w, "savelayout")) return .layout;
    if (eq(w, "formula") or eq(w, "formulas")) return .formula;
    if (eq(w, "format") or eq(w, "formats")) return .format;
    if (eq(w, "exec") or eq(w, "do") or eq(w, "run")) return .exec;
    // Longer first: verbOf matches whole words, so "script-run" can never be read as "script".
    if (eq(w, "script-run") or eq(w, "scriptrun") or eq(w, "runscript")) return .script_run;
    if (eq(w, "script") or eq(w, "stc")) return .script;
    if (eq(w, "undo") or eq(w, "u")) return .undo;
    if (eq(w, "redo") or eq(w, "r")) return .redo;
    if (eq(w, "reset") or eq(w, "revert")) return .reset;
    if (eq(w, "drop") or eq(w, "close") or eq(w, "forget")) return .drop;
    if (eq(w, "clear") or eq(w, "cls")) return .clear;
    if (eq(w, "copy") or eq(w, "yank")) return .copy;
    if (eq(w, "paste")) return .paste;
    if (eq(w, "unpaste") or eq(w, "pop")) return .unpaste;
    if (eq(w, "images") or eq(w, "attachments") or eq(w, "attached")) return .images;
    if (eq(w, "theme") or eq(w, "themes")) return .theme;
    if (eq(w, "mouse")) return .mouse;
    if (eq(w, "reveal-speed")) return .reveal_speed;
    if (eq(w, "status") or eq(w, "info") or eq(w, "image")) return .status;
    if (eq(w, "help") or eq(w, "?") or eq(w, "h")) return .help;
    if (eq(w, "exit") or eq(w, "quit") or eq(w, "q")) return .quit;
    // Server connections.
    if (eq(w, "connect")) return .connect;
    if (eq(w, "disconnect")) return .disconnect;
    if (eq(w, "reconnect") or eq(w, "refresh")) return .reconnect;
    if (eq(w, "connections") or eq(w, "servers")) return .connections;
    if (eq(w, "projects") or eq(w, "ls")) return .projects;
    if (eq(w, "project-color") or eq(w, "projectcolor") or eq(w, "pcolor")) return .project_color;
    if (eq(w, "blank-color") or eq(w, "blankcolor") or eq(w, "bcolor")) return .blank_color;
    if (eq(w, "project-description") or eq(w, "projectdescription") or eq(w, "description") or eq(w, "desc")) return .project_description;
    // Keyword commands (order-independent — verbOf matches the whole word). More-specific
    // variants first for readability; exact matching means suffixes never shadow "keywords".
    if (eq(w, "keywords-search") or eq(w, "keywordssearch") or eq(w, "kwsearch")) return .keywords_search;
    if (eq(w, "keywords-add") or eq(w, "keywordsadd") or eq(w, "kwadd")) return .keywords_add;
    if (eq(w, "keywords-del") or eq(w, "keywords-delete") or eq(w, "keywords-rm") or eq(w, "kwdel")) return .keywords_del;
    if (eq(w, "keywords") or eq(w, "kw")) return .keywords;
    if (eq(w, "rename") or eq(w, "mv")) return .rename;
    // LLM assistant: send a prompt / configure the provider (llm-contract.md).
    if (eq(w, "prompt") or eq(w, "p")) return .prompt;
    if (eq(w, "llm")) return .llm;
    // Opt-in per-project chat persistence (llm-contract.md §12).
    if (eq(w, "chat")) return .chat;
    if (eq(w, "expire") or eq(w, "expiry") or eq(w, "ttl")) return .expire;
    if (eq(w, "fetch") or eq(w, "pull")) return .fetch;
    if (eq(w, "sync")) return .sync;
    // The other apps: open this session there, or share a .stencil file with the desktop live.
    if (eq(w, "browser") or eq(w, "web")) return .browser;
    if (eq(w, "desktop") or eq(w, "app")) return .desktop;
    if (eq(w, "desktopsync") or eq(w, "desktop-sync") or eq(w, "link")) return .desktopsync;
    return null;
}

const testing = std.testing;

test "verbs: the app hand-offs and their aliases" {
    try testing.expectEqual(Verb.browser, verbOf("browser").?);
    try testing.expectEqual(Verb.browser, verbOf("WEB").?);
    try testing.expectEqual(Verb.desktop, verbOf("desktop").?);
    try testing.expectEqual(Verb.desktop, verbOf("app").?);
    try testing.expectEqual(Verb.desktopsync, verbOf("desktopsync").?);
    try testing.expectEqual(Verb.desktopsync, verbOf("desktop-sync").?);
    try testing.expectEqual(Verb.desktopsync, verbOf("link").?);
    try testing.expect(verbOf("desktops") == null);
}
