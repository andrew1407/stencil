//! The operator's server allowlist (`STENCIL_MCP_SERVERS`) and token map
//! (`STENCIL_MCP_SERVER_TOKENS`): what a model-chosen server URL may reach, and as whom.

use stencil_mcp::config::{origin_of, Servers};

fn servers(list: &str, tokens: &str) -> (Servers, Vec<String>) {
    let mut warnings = Vec::new();
    let servers = Servers::parse(Some(list), Some(tokens), &mut warnings);
    (servers, warnings)
}

#[test]
fn an_origin_is_scheme_host_and_a_non_default_port() {
    let spelled = origin_of("http://Host.Example:8090/projects#token=x").unwrap();
    assert_eq!(spelled, "http://host.example:8090");
    assert_eq!(origin_of("https://h.example:443").unwrap(), "https://h.example");
    assert_eq!(origin_of("localhost:8090").unwrap(), "http://localhost:8090");
    assert_eq!(origin_of("remote.example:8090").unwrap(), "https://remote.example:8090");
    assert_eq!(origin_of("http://[::1]:8090/").unwrap(), "http://[::1]:8090");
    assert!(origin_of("http://user:pw@h.example").is_err(), "credentials in the URL");
    assert!(origin_of("ftp://h.example").is_err());
    assert!(origin_of("http://h.example:99999").is_err());
}

#[test]
fn the_allowlist_maps_each_origin_to_its_token() {
    let (servers, warnings) = servers(
        "http://localhost:8090, https://team.example",
        "http://localhost:8090=local-tok,https://team.example:443=team=tok",
    );
    assert!(warnings.is_empty(), "{warnings:?}");
    let local = servers.find("server", "http://127.0.0.1:8090").err();
    assert!(local.is_some(), "an unlisted spelling of a host is a different origin");

    let local = servers.find("server", "localhost:8090/ignored/path").unwrap();
    assert_eq!(local.origin, "http://localhost:8090");
    assert_eq!(local.token.as_deref(), Some("local-tok"));
    let team = servers.find("remote", "https://TEAM.example/").unwrap();
    assert_eq!(team.token.as_deref(), Some("team=tok"), "only the first `=` splits a pair");
}

#[test]
fn a_url_off_the_allowlist_is_refused_naming_what_is_allowed() {
    let (servers, _) = servers("http://localhost:8090", "");
    let error = servers.find("remote", "http://attacker.test:8090").unwrap_err();
    let refused = "`remote` 'http://attacker.test:8090' is not an allowed server";
    assert!(error.contains(refused), "{error}");
    assert!(error.contains("http://localhost:8090"), "{error}");
}

#[test]
fn without_an_allowlist_every_server_is_refused() {
    let error = Servers::default().find("server", "http://localhost:8090").unwrap_err();
    assert!(error.contains("STENCIL_MCP_SERVERS"), "{error}");
    assert!(error.contains("none is configured"), "{error}");
}

#[test]
fn bad_entries_and_orphan_tokens_are_warnings_not_servers() {
    let tokens = "http://other.test=zz-secret, nopair";
    let (servers, warnings) = servers("ftp://x, http://ok.test", tokens);
    assert_eq!(servers.origins(), ["http://ok.test"]);
    assert_eq!(warnings.len(), 3, "{warnings:?}");
    let leaked = warnings.iter().any(|w| w.contains("zz-secret"));
    assert!(!leaked, "a warning echoed a token: {warnings:?}");
}
