//! The console's Tab-completion list against its grammar: every word offered is a command, and
//! every verb is offered.
const std = @import("std");
const commands = @import("../../src/console/commands.zig");
const completions = @import("../../src/console/ui.zig").completions;

test "completions: every console command is offered by Tab-complete" {
    // Everything offered must really BE a command, so a typo can't complete to nothing.
    for (completions) |w| {
        if (commands.verbOf(w) != null) continue;
        if (commands.actionOf(w, "") != null) continue;
        std.debug.print("completion '{s}' is not a command\n", .{w});
        return error.UnknownCompletion;
    }
    // …and every verb must be reachable from the list, so a new command can't ship without
    // Tab-completion (this is how /reveal, /chat and /project-description were found missing).
    var missing = false;
    inline for (std.enums.values(commands.Verb)) |want| {
        var found = false;
        for (completions) |w| {
            if (commands.verbOf(w)) |v| {
                if (v == want) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            std.debug.print("no completion offers the '{s}' command\n", .{@tagName(want)});
            missing = true;
        }
    }
    if (missing) return error.MissingCompletion;
}
