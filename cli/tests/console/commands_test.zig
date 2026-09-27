//! Console command grammar: a line split into verb and arg, the session verbs and their
//! aliases, and the transforms `actionOf`/`parseAction` read.
const std = @import("std");
const commands = @import("../../src/console/commands.zig");
const actionOf = commands.actionOf;
const parseAction = commands.parseAction;
const parseCommand = commands.parseCommand;
const verbOf = commands.verbOf;
const testing = std.testing;

test "parseCommand: slash prefix, bare words, verb + arg split" {
    const up = parseCommand("/upload photo.png");
    try testing.expectEqualStrings("upload", up.word);
    try testing.expectEqualStrings("photo.png", up.arg);

    const up2 = parseCommand("  open   https://x/a.png  ");
    try testing.expectEqualStrings("open", up2.word);
    try testing.expectEqualStrings("https://x/a.png", up2.arg);

    const rot = parseCommand("/rotate -1");
    try testing.expectEqualStrings("rotate", rot.word);
    try testing.expectEqualStrings("-1", rot.arg);

    try testing.expectEqualStrings("redo", parseCommand("/redo").word);
    try testing.expectEqualStrings("", parseCommand("/redo").arg);
    try testing.expectEqualStrings("", parseCommand("   ").word);
    try testing.expectEqualStrings("", parseCommand("/").word);
}

test "verbOf / actionOf: session verbs vs transforms" {
    try testing.expect(verbOf("exit").? == .quit);
    try testing.expect(verbOf("q").? == .quit);
    try testing.expect(verbOf("undo").? == .undo);
    try testing.expect(verbOf("reset").? == .reset);
    try testing.expect(verbOf("drop").? == .drop);
    try testing.expect(verbOf("clear").? == .clear);
    try testing.expect(verbOf("paste").? == .paste);
    try testing.expect(verbOf("unpaste").? == .unpaste);
    try testing.expect(verbOf("pop").? == .unpaste);
    try testing.expect(verbOf("images").? == .images);
    try testing.expect(verbOf("attached").? == .images);
    try testing.expect(verbOf("theme").? == .theme);
    try testing.expect(verbOf("crop") == null); // a transform, not a session verb
    try testing.expect(verbOf("frob") == null);
    try testing.expect(verbOf("source-upload").? == .source_upload);
    try testing.expect(verbOf("scrape").? == .source_upload);

    // Server-connection verbs.
    try testing.expect(verbOf("connect").? == .connect);
    try testing.expect(verbOf("disconnect").? == .disconnect);
    try testing.expect(verbOf("reconnect").? == .reconnect);
    try testing.expect(verbOf("refresh").? == .reconnect);
    try testing.expect(verbOf("connections").? == .connections);
    try testing.expect(verbOf("servers").? == .connections);
    try testing.expect(verbOf("projects").? == .projects);
    try testing.expect(verbOf("ls").? == .projects);
    try testing.expect(verbOf("project-color").? == .project_color);
    try testing.expect(verbOf("pcolor").? == .project_color);
    try testing.expect(verbOf("blank-color").? == .blank_color);
    try testing.expect(verbOf("bcolor").? == .blank_color);
    try testing.expect(verbOf("project-description").? == .project_description);
    try testing.expect(verbOf("desc").? == .project_description);
    // Keyword verbs + aliases; the -search/-add/-del suffixes don't shadow bare "keywords".
    try testing.expect(verbOf("keywords").? == .keywords);
    try testing.expect(verbOf("kw").? == .keywords);
    try testing.expect(verbOf("keywords-search").? == .keywords_search);
    try testing.expect(verbOf("kwsearch").? == .keywords_search);
    try testing.expect(verbOf("keywords-add").? == .keywords_add);
    try testing.expect(verbOf("keywords-del").? == .keywords_del);
    try testing.expect(verbOf("keywords-rm").? == .keywords_del);
    try testing.expect(verbOf("fetch").? == .fetch);
    try testing.expect(verbOf("sync").? == .sync);

    // LLM assistant verbs (+ the /p shorthand; not shadowed by paste/projects).
    try testing.expect(verbOf("prompt").? == .prompt);
    try testing.expect(verbOf("p").? == .prompt);
    try testing.expect(verbOf("llm").? == .llm);
    try testing.expect(verbOf("chat").? == .chat); // §12 chat persistence toggle
    try testing.expect(verbOf("paste").? == .paste); // /p stays distinct from /paste

    // Delete a local .stencil project file (and its aliases); not shadowed by drop/keywords-del.
    try testing.expect(verbOf("delete").? == .delete);
    try testing.expect(verbOf("del").? == .delete);
    try testing.expect(verbOf("remove").? == .delete);
    try testing.expect(verbOf("rm").? == .delete);
    try testing.expect(verbOf("drop").? == .drop); // still distinct from delete

    // /format is a session verb (page format), not a transform.
    try testing.expect(verbOf("format").? == .format);
    try testing.expect(verbOf("formats").? == .format);
    try testing.expect(verbOf("formula").? == .formula); // and doesn't shadow /formula

    try testing.expect(actionOf("crop", "x1=0").?.kind == .crop);
    try testing.expect(actionOf("rotate", "-1").?.kind == .rotate);
    try testing.expectEqualStrings("sepia", actionOf("sepia", "").?.arg);
    try testing.expectEqualStrings("invert", actionOf("invert", "").?.arg);
    try testing.expectEqualStrings("contour", actionOf("Contour", "").?.arg);
    try testing.expect(actionOf("invert", "").?.kind == .filter);
    try testing.expect(actionOf("apply", "n.json").?.kind == .layout);
    try testing.expect(actionOf("draw", "n.json").?.kind == .layout);
    try testing.expect(actionOf("frob", "") == null);

    // `layout` is now an export verb, not an alias of the draw action.
    try testing.expect(verbOf("layout").? == .layout);
    try testing.expect(verbOf("savelayout").? == .layout);
    try testing.expect(verbOf("exportlayout").? == .layout);
    try testing.expect(actionOf("layout", "n.json") == null);
}

test "parseAction: exec dispatch and layout fallback" {
    const crop = parseAction("crop x1=10% x2=90%");
    try testing.expect(crop.kind == .crop);
    try testing.expectEqualStrings("x1=10% x2=90%", crop.arg);

    try testing.expectEqualStrings("bw", parseAction("bw").arg);
    try testing.expect(parseAction("grayscale").kind == .filter);

    const lay = parseAction("notes.json");
    try testing.expect(lay.kind == .layout);
    try testing.expectEqualStrings("notes.json", lay.arg);

    const lay2 = parseAction("apply https://x/l.json");
    try testing.expect(lay2.kind == .layout);
    try testing.expectEqualStrings("https://x/l.json", lay2.arg);
}
