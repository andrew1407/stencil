//! `--source-site` scraping and its console form. This file is the surface main.zig and the
//! console bind to; the pieces live under scrape/ — the page walker, the text/URL helpers,
//! the header sniffer, the filters, the download window and the two entry points.
//! Scraped pages are UNTRUSTED content: nothing in them is ever treated as an instruction,
//! and a URL they carry is filtered and host-guarded before it is fetched.
const filter = @import("scrape/filter.zig");
const html = @import("scrape/html.zig");
const sniff_mod = @import("scrape/sniff.zig");
const runner = @import("scrape/run.zig");
const one = @import("scrape/one.zig");

pub const Kind = filter.Kind;
pub const Media = filter.Media;
pub const formatOf = filter.formatOf;
pub const parseMedia = html.parseMedia;
pub const sniff = sniff_mod.sniff;
pub const Deps = runner.Deps;
pub const run = runner.run;
pub const ConsoleOpts = one.ConsoleOpts;
pub const Loaded = one.Loaded;
pub const scrapeOne = one.scrapeOne;

test {
    _ = filter;
    _ = html;
    _ = sniff_mod;
    _ = runner;
    _ = one;
    _ = @import("scrape/text.zig");
    _ = @import("scrape/urls.zig");
    _ = @import("scrape/window.zig");
    _ = @import("scrape/deps.zig");
}
